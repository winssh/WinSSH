// Shared helpers for the M0 SSH library spike.
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

struct Args
{
    std::string host;
    std::string user;
    int port = 22;
    std::string keyFile; // private key path; the spike never reads it, only passes it to the library
};

inline std::string HomeDir()
{
    const char *p = std::getenv("USERPROFILE");
    return p ? std::string(p) : std::string(".");
}

inline Args ParseArgs(int argc, char **argv)
{
    Args a;
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s user@host [port] [keyfile]\n", argv[0]);
        std::exit(2);
    }
    std::string target = argv[1];
    auto at = target.find('@');
    a.user = target.substr(0, at);
    a.host = target.substr(at + 1);
    if (argc > 2)
        a.port = std::atoi(argv[2]);
    a.keyFile = argc > 3 ? argv[3] : HomeDir() + "\\.ssh\\id_ed25519";
    return a;
}

inline void Report(bool ok, const char *check, const std::string &detail)
{
    std::printf("[%s] %-28s %s\n", ok ? "PASS" : "FAIL", check, detail.c_str());
    std::fflush(stdout);
}

inline void Info(const char *check, const std::string &detail)
{
    std::printf("[INFO] %-28s %s\n", check, detail.c_str());
    std::fflush(stdout);
}

class Stopwatch
{
public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}
    long long Ms() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

// Make remote output readable in the report: show control characters as escapes.
inline std::string Printable(const std::string &s)
{
    std::string out;
    for (unsigned char c : s)
    {
        if (c == '\n')
            out += "\\n\n          ";
        else if (c == '\r')
            out += "\\r";
        else if (c == 0x1b)
            out += "\\e";
        else if (c < 0x20)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "\\x%02x", c);
            out += b;
        }
        else
            out += static_cast<char>(c);
    }
    return out;
}

inline std::string Base64NoPad(const unsigned char *data, size_t len)
{
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < len; i += 3)
    {
        unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
    }
    if (i < len)
    {
        unsigned v = data[i] << 16;
        if (i + 1 < len)
            v |= data[i + 1] << 8;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        if (i + 1 < len)
            out += tbl[(v >> 6) & 63];
    }
    return out;
}

// Command run with a PTY via exec (not an interactive shell, so the user's shell prompt and
// startup time don't affect the result). The client sends window-change 120x40 after the
// first "stty size" line arrives; the second "stty size" must show the new size.
constexpr const char *kPtyCmd = "echo TERM=$TERM; stty size; sleep 2; stty size; printf '\\342\\234\\223 utf8\\n'; exit 7";
