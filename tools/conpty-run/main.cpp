// conpty-run: runs a console program inside a Windows pseudo console (ConPTY) and drives it with
// a small script, so WinSSH can be tested end to end like a user in Windows Terminal.
//
// Usage: conpty-run [--cols N] [--rows N] [--timeout SEC] [--log FILE] --script FILE -- command [args...]
//
// Script (one command per line, '#' starts a comment):
//   send <text>        type text; escapes: \r \n \t \e \xNN \\  (e.g. "send exit\r", "send \x03")
//   expect <text>      wait until <text> appears in the output (VT sequences stripped)
//   expect-raw <text>  same, but search the raw output (escapes allowed, e.g. \e[?1049h)
//   mark               remember the current end of the raw output
//   reject-raw <text>  fail if <text> appears in the raw output since the last mark
//   expect-file <glob> <text>   some file matching <glob> contains <text> (e.g. a log, after exit)
//   reject-file <glob> <text>   files match <glob>, and none contains <text>
//   resize <C>x<R>     resize the pseudo console
//   sleep <ms>
//   copy <src> <dst>   overwrite a file (e.g. a config file the program watches)
//   exit <code>        wait for the program to exit and check its exit code
#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

std::string Unescape(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '\\' || i + 1 >= s.size())
        {
            out += s[i];
            continue;
        }
        const char c = s[++i];
        switch (c)
        {
        case 'r': out += '\r'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'e': out += '\x1b'; break;
        case '\\': out += '\\'; break;
        case 'x':
            if (i + 2 < s.size())
            {
                out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                i += 2;
            }
            break;
        default: out += '\\'; out += c; break;
        }
    }
    return out;
}

// Removes CSI, OSC and other escape sequences, keeping printable text.
class VtStripper
{
public:
    void Feed(const char *data, size_t n, std::string &out)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const char c = data[i];
            switch (state_)
            {
            case State::Text:
                if (c == '\x1b')
                    state_ = State::Esc;
                else
                    out += c;
                break;
            case State::Esc:
                if (c == '[')
                    state_ = State::Csi;
                else if (c == ']')
                    state_ = State::Osc;
                else
                    state_ = State::Text; // two-byte sequence
                break;
            case State::Csi:
                if (c >= 0x40 && c <= 0x7e)
                    state_ = State::Text;
                break;
            case State::Osc:
                if (c == '\x07')
                    state_ = State::Text;
                else if (c == '\x1b')
                    state_ = State::OscEsc;
                break;
            case State::OscEsc:
                state_ = State::Text; // ESC \ ends OSC
                break;
            }
        }
    }

private:
    enum class State { Text, Esc, Csi, Osc, OscEsc } state_ = State::Text;
};

struct Shared
{
    std::mutex m;
    std::condition_variable cv;
    std::string raw;
    std::string text;
    bool closed = false;
};

std::wstring Widen(const std::string &s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const wchar_t *w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring QuoteArg(const std::wstring &a)
{
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos)
        return a;
    std::wstring q = L"\"";
    for (wchar_t c : a)
    {
        if (c == L'"')
            q += L'\\';
        q += c;
    }
    return q + L"\"";
}

int Usage()
{
    std::fprintf(stderr, "usage: conpty-run [--cols N] [--rows N] [--timeout SEC] [--log FILE] --script FILE -- command [args...]\n");
    return 2;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    SHORT cols = 80, rows = 24;
    int timeoutSec = 20;
    std::string scriptPath, logPath;
    std::wstring cmdline;

    int i = 1;
    for (; i < argc; ++i)
    {
        const std::wstring a = argv[i];
        if (a == L"--")
        {
            ++i;
            break;
        }
        if (i + 1 >= argc)
            return Usage();
        if (a == L"--cols") cols = static_cast<SHORT>(_wtoi(argv[++i]));
        else if (a == L"--rows") rows = static_cast<SHORT>(_wtoi(argv[++i]));
        else if (a == L"--timeout") timeoutSec = _wtoi(argv[++i]);
        else if (a == L"--script") scriptPath = Narrow(argv[++i]);
        else if (a == L"--log") logPath = Narrow(argv[++i]);
        else return Usage();
    }
    for (; i < argc; ++i)
    {
        if (!cmdline.empty())
            cmdline += L' ';
        cmdline += QuoteArg(argv[i]);
    }
    if (scriptPath.empty() || cmdline.empty())
        return Usage();

    std::ifstream scriptFile(Widen(scriptPath));
    if (!scriptFile)
    {
        std::fprintf(stderr, "cannot read script %s\n", scriptPath.c_str());
        return 2;
    }

    // Pipes: we write keys to inWrite, the pseudo console writes output to outWrite.
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    CreatePipe(&inRead, &inWrite, nullptr, 0);
    CreatePipe(&outRead, &outWrite, nullptr, 0);
    HPCON pc = nullptr;
    if (FAILED(CreatePseudoConsole({cols, rows}, inRead, outWrite, 0, &pc)))
    {
        std::fprintf(stderr, "CreatePseudoConsole failed\n");
        return 2;
    }
    CloseHandle(inRead);
    CloseHandle(outWrite);

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    // Without this, a child of a process whose stdio is redirected inherits those pipes instead of
    // getting the pseudo console as its stdin/stdout.
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize);
    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof(pc), nullptr,
                              nullptr);
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmdline;
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr,
                        nullptr, &si.StartupInfo, &pi))
    {
        std::fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
        return 2;
    }

    Shared shared;
    std::thread reader([&] {
        VtStripper strip;
        char buf[8192];
        DWORD n = 0;
        while (ReadFile(outRead, buf, sizeof(buf), &n, nullptr) && n > 0)
        {
            std::lock_guard<std::mutex> lock(shared.m);
            shared.raw.append(buf, n);
            strip.Feed(buf, n, shared.text);
            shared.cv.notify_all();
        }
        std::lock_guard<std::mutex> lock(shared.m);
        shared.closed = true;
        shared.cv.notify_all();
    });

    size_t textPos = 0, rawPos = 0, markPos = 0;
    int failures = 0, lineNo = 0;
    std::string line;
    auto deadline = [&] { return std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec); };

    while (std::getline(scriptFile, line))
    {
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        const auto sp = line.find(' ');
        const std::string cmd = line.substr(0, sp);
        const std::string arg = sp == std::string::npos ? "" : line.substr(sp + 1);
        bool ok = true;
        std::string detail;

        if (cmd == "send")
        {
            const std::string bytes = Unescape(arg);
            DWORD w = 0;
            ok = WriteFile(inWrite, bytes.data(), static_cast<DWORD>(bytes.size()), &w, nullptr) != FALSE;
        }
        else if (cmd == "expect" || cmd == "expect-raw")
        {
            const bool raw = cmd == "expect-raw";
            const std::string needle = raw ? Unescape(arg) : arg;
            std::unique_lock<std::mutex> lock(shared.m);
            size_t &pos = raw ? rawPos : textPos;
            bool found = false;
            shared.cv.wait_until(lock, deadline(), [&] {
                const std::string &hay = raw ? shared.raw : shared.text;
                const auto f = hay.find(needle, pos);
                if (f != std::string::npos)
                {
                    pos = f + needle.size();
                    found = true;
                }
                return found || shared.closed;
            });
            ok = found;
            if (!ok)
                detail = shared.closed ? "(output closed)" : "(timeout)";
        }
        else if (cmd == "resize")
        {
            const auto x = arg.find('x');
            const COORD size{static_cast<SHORT>(std::stoi(arg.substr(0, x))), static_cast<SHORT>(std::stoi(arg.substr(x + 1)))};
            ok = SUCCEEDED(ResizePseudoConsole(pc, size));
        }
        else if (cmd == "mark")
        {
            // Later reject-raw checks only look at output after this point.
            std::lock_guard<std::mutex> lock(shared.m);
            markPos = shared.raw.size();
        }
        else if (cmd == "reject-raw")
        {
            const std::string needle = Unescape(arg);
            std::lock_guard<std::mutex> lock(shared.m);
            const auto f = shared.raw.find(needle, markPos);
            ok = f == std::string::npos;
            if (!ok)
                detail = "(found at raw offset " + std::to_string(f) + ")";
        }
        else if (cmd == "expect-file" || cmd == "reject-file")
        {
            // expect-file <glob> <text>: some file matching <glob> contains <text>
            // reject-file <glob> <text>: no file matching <glob> contains <text>
            const auto sp2 = arg.find(' ');
            const std::string pattern = arg.substr(0, sp2);
            const std::string needle = sp2 == std::string::npos ? "" : Unescape(arg.substr(sp2 + 1));
            std::wstring wpattern = Widen(pattern);
            for (auto &ch : wpattern)
                if (ch == L'/')
                    ch = L'\\';
            const auto slash = wpattern.find_last_of(L'\\');
            const std::wstring dir = slash == std::wstring::npos ? L"" : wpattern.substr(0, slash + 1);
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW(wpattern.c_str(), &fd);
            int files = 0;
            bool found = false;
            if (h != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        continue;
                    ++files;
                    std::ifstream in(dir + fd.cFileName, std::ios::binary);
                    std::stringstream ss;
                    ss << in.rdbuf();
                    if (ss.str().find(needle) != std::string::npos)
                        found = true;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            ok = cmd == "expect-file" ? found : (files > 0 && !found);
            detail = "(" + std::to_string(files) + " file(s) matched)";
        }
        else if (cmd == "copy")
        {
            // copy <src> <dst>: rewrites dst with src's content (new timestamp), e.g. to change a
            // config file while the program runs. Paths are relative to the current directory.
            const auto sp2 = arg.find(' ');
            std::ifstream in(Widen(arg.substr(0, sp2)), std::ios::binary);
            std::ofstream out(Widen(sp2 == std::string::npos ? "" : arg.substr(sp2 + 1)),
                              std::ios::binary | std::ios::trunc);
            out << in.rdbuf();
            ok = in.good() && out.good();
        }
        else if (cmd == "sleep")
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(arg)));
        }
        else if (cmd == "exit")
        {
            const DWORD w = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeoutSec) * 1000);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            ok = w == WAIT_OBJECT_0 && static_cast<int>(code) == std::stoi(arg);
            detail = w == WAIT_OBJECT_0 ? "(exit code " + std::to_string(code) + ")" : "(still running)";
        }
        else
        {
            ok = false;
            detail = "(unknown command)";
        }

        std::printf("[%s] %3d: %s %s\n", ok ? "PASS" : "FAIL", lineNo, line.c_str(), detail.c_str());
        std::fflush(stdout);
        if (!ok)
        {
            ++failures;
            break; // later steps depend on earlier ones
        }
    }

    // Stop the program if the script did not wait for it, then drain and close the pseudo console.
    if (WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT)
        TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    ClosePseudoConsole(pc);
    reader.join();
    CloseHandle(inWrite);
    CloseHandle(outRead);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteProcThreadAttributeList(si.lpAttributeList);

    if (!logPath.empty())
    {
        std::ofstream log(Widen(logPath), std::ios::binary);
        log << shared.raw;
    }
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
