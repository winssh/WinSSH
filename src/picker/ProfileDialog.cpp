#include "picker/ProfileDialog.h"

#include "resources/resource.h"
#include "util/Paths.h"
#include "util/Utf8.h"

#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")

namespace winssh::picker
{
namespace
{

HWND g_terminal = nullptr;

struct Context
{
    ProfileForm *form = nullptr;
    const std::vector<config::Profile> *existing = nullptr;
    DialogResult result = DialogResult::Cancel;
    HBRUSH errorBrush = nullptr;
};

std::string GetText(HWND dlg, int id)
{
    HWND ctl = ::GetDlgItem(dlg, id);
    const int len = ::GetWindowTextLengthW(ctl);
    std::wstring w(static_cast<size_t>(len) + 1, L'\0');
    ::GetWindowTextW(ctl, w.data(), len + 1);
    w.resize(static_cast<size_t>(len));
    std::string s = util::WideToUtf8(w);
    ::SecureZeroMemory(w.data(), w.size() * sizeof(wchar_t)); // may be the password
    return s;
}

void SetText(HWND dlg, int id, const std::string &utf8)
{
    ::SetDlgItemTextW(dlg, id, util::Utf8ToWide(utf8).c_str());
}

void SetCue(HWND dlg, int id, const wchar_t *text)
{
    ::SendDlgItemMessageW(dlg, id, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(text));
}

// Windows only lets the foreground process move focus to another window; attaching to the
// foreground thread's input makes SetForegroundWindow work from a console app in Windows Terminal.
void BringToFront(HWND hwnd)
{
    if (!hwnd || !::IsWindow(hwnd))
        return;
    HWND fg = ::GetForegroundWindow();
    const DWORD me = ::GetCurrentThreadId();
    const DWORD fgThread = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
    const bool attach = fgThread && fgThread != me && ::AttachThreadInput(me, fgThread, TRUE);
    ::ShowWindow(hwnd, ::IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    ::BringWindowToTop(hwnd);
    ::SetForegroundWindow(hwnd);
    if (attach)
        ::AttachThreadInput(me, fgThread, FALSE);
}

// Centers the dialog on the terminal window, kept inside that monitor's work area.
void CenterOnTerminal(HWND dlg)
{
    RECT dr{};
    ::GetWindowRect(dlg, &dr);
    RECT anchor{};
    HMONITOR mon = nullptr;
    if (g_terminal && ::IsWindow(g_terminal) && ::IsWindowVisible(g_terminal) && !::IsIconic(g_terminal))
    {
        ::GetWindowRect(g_terminal, &anchor);
        mon = ::MonitorFromWindow(g_terminal, MONITOR_DEFAULTTONEAREST);
    }
    else
    {
        POINT pt{};
        ::GetCursorPos(&pt);
        mon = ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO mi{sizeof(mi)};
    ::GetMonitorInfoW(mon, &mi);
    if (anchor.right <= anchor.left)
        anchor = mi.rcWork;
    const int w = dr.right - dr.left;
    const int h = dr.bottom - dr.top;
    int x = anchor.left + ((anchor.right - anchor.left) - w) / 2;
    int y = anchor.top + ((anchor.bottom - anchor.top) - h) / 2;
    x = std::max(static_cast<int>(mi.rcWork.left), std::min(x, static_cast<int>(mi.rcWork.right) - w));
    y = std::max(static_cast<int>(mi.rcWork.top), std::min(y, static_cast<int>(mi.rcWork.bottom) - h));
    ::SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Load(HWND dlg, const ProfileForm &f)
{
    SetText(dlg, IDC_PF_ID, f.id);
    SetText(dlg, IDC_PF_NAME, f.name);
    SetText(dlg, IDC_PF_HOST, f.host);
    SetText(dlg, IDC_PF_PORT, f.port);
    SetText(dlg, IDC_PF_USER, f.user);
    SetText(dlg, IDC_PF_KEY, f.identityFile);
    SetText(dlg, IDC_PF_HIGHLIGHT, f.highlight);
    SetText(dlg, IDC_PF_KEEPALIVE, f.keepAlive);
    ::CheckDlgButton(dlg, IDC_PF_SAVEPW, f.savePassword ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dlg, IDC_PF_LEGACY, f.legacyAlgorithms ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dlg, IDC_PF_RECONNECT, f.autoReconnect ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dlg, IDC_PF_LOG, f.logEnabled ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dlg, IDC_PF_LOG_TS, f.logTimestamps ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dlg, IDC_PF_SCREEN_TS, f.screenTimestamps ? BST_CHECKED : BST_UNCHECKED);
    ::EnableWindow(::GetDlgItem(dlg, IDC_PF_LOG_TS), f.logEnabled);

    SetCue(dlg, IDC_PF_NAME, L"same as ID");
    SetCue(dlg, IDC_PF_USER, L"your Windows user name");
    SetCue(dlg, IDC_PF_KEY, L"~/.ssh/id_ed25519 (default keys and agent)");
    SetCue(dlg, IDC_PF_HIGHLIGHT, L"default");
    SetCue(dlg, IDC_PF_PASSWORD, f.hasSavedPassword ? L"saved - leave empty to keep it" : L"optional");

    HWND combo = ::GetDlgItem(dlg, IDC_PF_POLICY);
    ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"strict - ask, refuse a changed key"));
    ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"accept-new - add unknown hosts"));
    ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"warn-on-change - lab devices"));
    const int sel = f.hostKeyPolicy == config::HostKeyPolicy::AcceptNew      ? 1
                    : f.hostKeyPolicy == config::HostKeyPolicy::WarnOnChange ? 2
                                                                             : 0;
    ::SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

void Read(HWND dlg, ProfileForm &f)
{
    f.id = GetText(dlg, IDC_PF_ID);
    f.name = GetText(dlg, IDC_PF_NAME);
    f.host = GetText(dlg, IDC_PF_HOST);
    f.port = GetText(dlg, IDC_PF_PORT);
    f.user = GetText(dlg, IDC_PF_USER);
    f.identityFile = GetText(dlg, IDC_PF_KEY);
    f.highlight = GetText(dlg, IDC_PF_HIGHLIGHT);
    f.keepAlive = GetText(dlg, IDC_PF_KEEPALIVE);
    f.savePassword = ::IsDlgButtonChecked(dlg, IDC_PF_SAVEPW) == BST_CHECKED;
    f.legacyAlgorithms = ::IsDlgButtonChecked(dlg, IDC_PF_LEGACY) == BST_CHECKED;
    f.autoReconnect = ::IsDlgButtonChecked(dlg, IDC_PF_RECONNECT) == BST_CHECKED;
    f.logEnabled = ::IsDlgButtonChecked(dlg, IDC_PF_LOG) == BST_CHECKED;
    f.logTimestamps = ::IsDlgButtonChecked(dlg, IDC_PF_LOG_TS) == BST_CHECKED;
    f.screenTimestamps = ::IsDlgButtonChecked(dlg, IDC_PF_SCREEN_TS) == BST_CHECKED;
    f.password = GetText(dlg, IDC_PF_PASSWORD);
    const auto sel = ::SendDlgItemMessageW(dlg, IDC_PF_POLICY, CB_GETCURSEL, 0, 0);
    f.hostKeyPolicy = sel == 1   ? config::HostKeyPolicy::AcceptNew
                      : sel == 2 ? config::HostKeyPolicy::WarnOnChange
                                 : config::HostKeyPolicy::Strict;
}

int ControlFor(int field)
{
    switch (field)
    {
    case kFieldId: return IDC_PF_ID;
    case kFieldHost: return IDC_PF_HOST;
    case kFieldPort: return IDC_PF_PORT;
    case kFieldKeepAlive: return IDC_PF_KEEPALIVE;
    case kFieldHighlight: return IDC_PF_HIGHLIGHT;
    default: return 0;
    }
}

void Browse(HWND dlg)
{
    wchar_t file[MAX_PATH] = L"";
    const std::wstring current = util::Utf8ToWide(util::ExpandHome(GetText(dlg, IDC_PF_KEY)));
    if (!current.empty() && current.size() < MAX_PATH)
        wcscpy_s(file, current.c_str());
    const std::wstring dir = util::SshDir().wstring();
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg;
    ofn.lpstrFilter = L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.c_str();
    ofn.lpstrTitle = L"Select a private key file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (::GetOpenFileNameW(&ofn))
        SetText(dlg, IDC_PF_KEY, util::WideToUtf8(file));
}

// Validates and closes with `result`, or shows the problem and keeps the dialog open.
void Finish(HWND dlg, Context &ctx, DialogResult result)
{
    Read(dlg, *ctx.form);
    const bool forSave = result != DialogResult::Connect;
    if (const auto err = ValidateForm(*ctx.form, *ctx.existing, forSave))
    {
        SetText(dlg, IDC_PF_ERROR, err->message);
        if (const int ctl = ControlFor(err->field))
        {
            ::SetFocus(::GetDlgItem(dlg, ctl));
            ::SendDlgItemMessageW(dlg, ctl, EM_SETSEL, 0, -1);
        }
        return;
    }
    ctx.result = result;
    ::EndDialog(dlg, 1);
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    auto *ctx = reinterpret_cast<Context *>(::GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        ctx = reinterpret_cast<Context *>(lp);
        ::SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
        Load(dlg, *ctx->form);
        if (!ctx->form->originalId.empty())
            ::SetWindowTextW(dlg, util::Utf8ToWide("WinSSH profile: " + ctx->form->originalId).c_str());
        else
            ::SetWindowTextW(dlg, L"WinSSH: new profile");
        ctx->errorBrush = ::GetSysColorBrush(COLOR_BTNFACE);
        CenterOnTerminal(dlg);
        BringToFront(dlg);
        ::SetFocus(::GetDlgItem(dlg, ctx->form->host.empty() ? IDC_PF_HOST : IDC_PF_ID));
        return FALSE; // focus set above
    }
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lp) == ::GetDlgItem(dlg, IDC_PF_ERROR))
        {
            HDC dc = reinterpret_cast<HDC>(wp);
            ::SetTextColor(dc, RGB(192, 0, 0));
            ::SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(ctx ? ctx->errorBrush : ::GetSysColorBrush(COLOR_BTNFACE));
        }
        return FALSE;
    case WM_COMMAND:
        if (!ctx)
            return FALSE;
        switch (LOWORD(wp))
        {
        case IDC_PF_HOST:
            // Suggest an id from the host when the id is still empty.
            if (HIWORD(wp) == EN_KILLFOCUS && GetText(dlg, IDC_PF_ID).empty() && !GetText(dlg, IDC_PF_HOST).empty())
                SetText(dlg, IDC_PF_ID, SuggestId(GetText(dlg, IDC_PF_HOST), *ctx->existing));
            return TRUE;
        case IDC_PF_BROWSE: Browse(dlg); return TRUE;
        case IDC_PF_LOG:
            // "with times on each line" belongs to the log file option.
            ::EnableWindow(::GetDlgItem(dlg, IDC_PF_LOG_TS), ::IsDlgButtonChecked(dlg, IDC_PF_LOG) == BST_CHECKED);
            return TRUE;
        case IDC_PF_CONNECT: Finish(dlg, *ctx, DialogResult::Connect); return TRUE;
        case IDC_PF_SAVE: Finish(dlg, *ctx, DialogResult::Save); return TRUE;
        case IDOK: Finish(dlg, *ctx, DialogResult::SaveAndConnect); return TRUE;
        case IDCANCEL:
            ctx->result = DialogResult::Cancel;
            ::EndDialog(dlg, 0);
            return TRUE;
        default: return FALSE;
        }
    default:
        return FALSE;
    }
}

} // namespace

void CaptureTerminalWindow()
{
    g_terminal = ::GetForegroundWindow();
}

DialogResult ShowProfileDialog(ProfileForm &form, const std::vector<config::Profile> &existing)
{
    static bool controlsReady = false;
    if (!controlsReady)
    {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
        ::InitCommonControlsEx(&icc);
        controlsReady = true;
    }
    Context ctx;
    ctx.form = &form;
    ctx.existing = &existing;
    // No owner: the terminal window belongs to another process (Windows Terminal), and a
    // cross-process owner can block input. The dialog is centered on it and brought to the front.
    ::DialogBoxParamW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PROFILE), nullptr, DialogProc,
                      reinterpret_cast<LPARAM>(&ctx));
    BringToFront(g_terminal);
    return ctx.result;
}

} // namespace winssh::picker
