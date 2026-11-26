# WinSSH — Plan

**SSH terminal for Windows, built to run as a Windows Terminal tab, with keyword highlighting you can configure.**

Status: draft. D1–D5 and D9 decided (see section 11).

---

## 1. Goals

| # | Goal | Source |
|---|---|---|
| G1 | Open an interactive SSH session (shell with a PTY) | Initial idea |
| G2 | Runs on Windows 10 1809+ / Windows 11 | Initial idea |
| G3 | Opens as a tab in Windows Terminal (like WinSerial) | Initial idea |
| G4 | Keyword highlighting on the output stream | Initial idea |
| G5 | User-defined highlight rules (keywords, regex, colors) | New |
| G6 | Connection history and saved connection profiles | New |

### Non-goals (v1)

- Being a terminal emulator. Windows Terminal renders; WinSSH only sits between the keyboard, the SSH channel, and the console.
- SFTP / SCP file transfer, X11 forwarding, a GUI session manager window.
- Running on Linux/macOS.

### Why build this instead of `ssh user@host` in Windows Terminal

Plain `ssh.exe` in Windows Terminal already works. WinSSH adds what it lacks:
highlighted output, saved profiles with stored credentials, one WT tab per profile,
and (later) session logging and auto-reconnect.

---

## 2. What we take from WinSerial

Repo: `C:\Users\BS\work\repos\WinSerial` (single `WinSerial.cpp`, ~830 lines).

| WinSerial piece | Reuse in WinSSH? | Notes |
|---|---|---|
| C++17 + CMake + vcpkg + MSVC, `Version.h.in` from git hash | Yes | Same toolchain, same CI shape |
| `boost::asio` async read/write loop between console and stream | Yes, rework | SSH channel replaces `serial_port` |
| Console setup (`ENABLE_VIRTUAL_TERMINAL_INPUT/PROCESSING`) | Yes | Plus raw input (no line/echo processing) |
| `ColorizeString()` — 4 hardcoded `std::regex` passes | Replace | See section 4; current approach can corrupt escape sequences and is slow |
| Line buffer + 50 ms debounce before printing | Replace | Fine for serial logs, adds visible lag to interactive shells |
| `Ctrl+A` command-mode hotkeys | Change key | `Ctrl+A` is "start of line" in bash and the tmux/screen prefix — collides on SSH |
| Local echo / line-edit mode | Drop | Remote PTY does echo and line editing |
| Settings in Registry (`HKCU`) | Replace | JSON files in `%APPDATA%\WinSSH\` (editable, easy to back up) |
| Win32 settings dialog | Optional | See section 6.3 |
| Inno Setup installer, portable zip, GitHub Actions release | Yes | Same pipeline |
| `Add-TerminalProfile.ps1` (regex-edits WT `settings.json`) | Replace | Use WT JSON fragments (section 7) |

Note: WinSerial has an uncommitted, staged SSH experiment (libssh2 + `implementation_plan.md`).
Useful as a reference for the libssh2 stream wrapper idea; it stores the SSH password in the
Registry in plain text, which WinSSH must not do.

---

## 3. Architecture

### 3.1 Two ways to talk SSH — decided: Option A (D1)

**Option A — In-process SSH library (libssh or libssh2)**

```
Windows Terminal tab
  └─ WinSSH.exe
       console input (VT) ──► input handler (hotkeys) ──► SSH channel (PTY)
       console output ◄── highlighter ◄────────────────── SSH channel
```

- \+ Full control: password / keyboard-interactive prompts, host-key prompt, reconnect, keepalive.
- \+ Highlighter sees the exact bytes from the server.
- \+ Passwords come straight from Windows Credential Manager.
- − We own more: `known_hosts`, key formats, agent support, algorithm negotiation for old devices.

**Option B — Wrap the built-in OpenSSH client (`ssh.exe`) in a ConPTY**

```
Windows Terminal tab
  └─ WinSSH.exe ──ConPTY──► ssh.exe user@host
       console output ◄── highlighter ◄── ConPTY output
```

- \+ Every OpenSSH feature for free: `~/.ssh/config`, ProxyJump, agent, certificates, port forwarding.
- \+ Least security-sensitive code on our side.
- − Two ConPTY layers (WT + ours): output is re-rendered once more; risk of redraw glitches in full-screen apps.
- − Saved passwords need `SSH_ASKPASS` + `SSH_ASKPASS_REQUIRE=force` with WinSSH as the askpass helper (needs a spike: support depends on the Win32-OpenSSH version).
- − Host-key and auth prompts are ssh.exe's text, not ours.

**Decision (D1):** Option A. The app's value is highlighting and profiles; both are cleaner when
we own the byte stream and the auth flow. The M0 spike picks the library (D2). Option B stays as
the fallback only if neither library can handle our real targets.

### 3.2 libssh vs libssh2 (D2, decided by the M0 spike)

Check each against real targets (CB2 dev unit, a Linux server, a network switch if available).

Spike code: `spike/ssh-lib/` (two console programs running the same checks; see its README).

**Run 1 — 2026-11-26.** Target: Linux dev server (Ubuntu 22.04, OpenSSH 8.9p1), user key
ed25519 (OpenSSH format, loaded in the Windows OpenSSH agent and also present as a file).
Libraries from vcpkg (baseline `38a77589`), x64-windows, OpenSSL 3.6 backend for both.

| Check | libssh 0.12.0 | libssh2 1.11.1 |
|---|---|---|
| Connect + handshake | ✅ ~160–240 ms | ✅ ~115–155 ms |
| Negotiated kex / host key (defaults) | `sntrup761x25519-sha512` / `ssh-ed25519` | `curve25519-sha256` / `ecdsa-sha2-nistp256` (prefers ECDSA; set `libssh2_session_method_pref` to put ed25519 first) |
| Cipher | chacha20-poly1305 | chacha20-poly1305 |
| `known_hosts` check | ✅ built-in (`ssh_session_is_known_server`) | ✅ `libssh2_knownhost_*` (read file + check; we write the "add/update" logic) |
| ed25519 key from file (no passphrase) | ✅ | ✅ |
| **Windows OpenSSH agent** (`\\.\pipe\openssh-ssh-agent`) | ❌ no support — agent code uses Unix sockets only; `SSH_AUTH_SOCK` pointing to the pipe fails | ✅ works out of the box (Pageant was not running, so this was the OpenSSH pipe) |
| PTY `xterm-256color` + `window-change` resize | ✅ | ✅ |
| UTF-8 passthrough, remote exit code | ✅ | ✅ |
| Parse `~/.ssh/config` | ✅ `ssh_options_parse_config` | ❌ not provided (our code; only needed for `--import-ssh-config`) |
| Legacy algorithms (`group14-sha1`, `group1-sha1`, `ssh-rsa`, `aes128-cbc`) | ✅ accepted by options | ✅ in supported list |
| Non-blocking API for `boost::asio` | ✅ `ssh_get_fd` + `ssh_set_blocking(0)`; note `ssh_channel_read_timeout` returns `SSH_AGAIN` (not 0) on timeout in 0.12 | ✅ socket is ours; `LIBSSH2_ERROR_EAGAIN` + `async_wait` (same idea as WinSerial's experiment) |
| License | LGPL-2.1 (must ship as DLL to stay simple with an MIT app) | BSD-3-Clause (static link fine) |
| keyboard-interactive | not tested (target has it off) | not tested |
| Passphrase-protected key file | not tested | not tested |
| CB2 unit | ✅ see run 2 | ✅ see run 2 |
| Network switch | not tested | not tested |

**Run 2 — 2026-11-26.** Target: CB2 unit E13F8 (`fc-cloudbox` distro, Yocto mickledore, kernel
6.1.36-imx8mp, aarch64), **OpenSSH 9.3** (not Dropbear), login shell `/bin/sh`, user `root`, same ed25519 key.

| Check | libssh 0.12.0 | libssh2 1.11.1 |
|---|---|---|
| Connect | ✅ 268 ms | ✅ 275 ms |
| Negotiated kex / host key | `sntrup761x25519-sha512` / `ssh-ed25519` | `curve25519-sha256` / `ecdsa-sha2-nistp256` |
| `known_hosts` check | ✅ match | ✅ match |
| Agent auth | ❌ (same as run 1) | ✅ 67 ms |
| Key file auth | ✅ | ✅ |
| PTY + resize, UTF-8, exit code | ✅ | ✅ |

Same picture as run 1. Note for M1: libssh2 matched `known_hosts` only because the file also holds an
ECDSA entry for the unit. Users whose `known_hosts` has only the ed25519 key (OpenSSH's default choice)
would get a false "unknown host". **Set the host-key preference to put `ssh-ed25519` first**
(`libssh2_session_method_pref(LIBSSH2_METHOD_HOSTKEY, ...)`) so WinSSH and `ssh.exe` agree.

**Result:** both libraries cover the core session (handshake, host key, key auth, PTY, resize, UTF-8,
exit code). The deciding difference is the **Windows OpenSSH agent**: libssh2 uses it directly, libssh
would need our own agent client over the named pipe. libssh2's license is also simpler for an MIT app.
libssh's extras (`~/.ssh/config` parsing, newer kex, ed25519 host key first) are nice but either small
for us to add or only a preference setting.

**Decision D2 (2026-11-26): libssh2.** CB2 checked in run 2 (pass). Still to check in M1:
a network switch with legacy algorithms, keyboard-interactive, and a passphrase-protected key.
If one of these fails, revisit D2.

### 3.3 Module layout (no more single 800-line file)

```
src/
  main.cpp              top-level flow
  cli/                  command-line parsing
  util/                 small helpers (UTF-8 conversion, ...)
  resources/            icon, VERSIONINFO, application manifest
  console/              raw VT console in/out, resize events, title
  ssh/                  session, auth, channel, host-key check, keepalive
  highlight/            VT-aware tokenizer, rule engine, built-in rule sets
  config/               settings, profiles, history, highlight rules (JSON load/save, validation)
  credentials/          Windows Credential Manager wrapper
  hotkeys/              escape-key command mode
  wt/                   Windows Terminal fragment generator
tests/                  unit tests (highlighter, config, CLI parsing)
installer/              Inno Setup script
```

Later option: move `console/`, `highlight/`, `hotkeys/`, `wt/` into a small shared library and
let WinSerial use it too.

### 3.4 Session I/O details that are easy to get wrong

- **Raw input:** turn off `ENABLE_LINE_INPUT`, `ENABLE_ECHO_INPUT`, `ENABLE_PROCESSED_INPUT`; keep
  `ENABLE_VIRTUAL_TERMINAL_INPUT` so arrow keys, function keys, and mouse come in as VT sequences and
  go to the server unchanged. `Ctrl+C` must go to the remote side, not kill WinSSH.
- **Window resize:** read `WINDOW_BUFFER_SIZE_EVENT` and send an SSH `window-change` request,
  otherwise `vim`, `htop`, `less` draw wrong after the tab is resized.
- **PTY request:** `xterm-256color`, current size, `LANG` if the server accepts it.
- **UTF-8:** set console code page to UTF-8; never split a multi-byte character when highlighting.
- **Exit:** return the remote exit status as the process exit code. With WT `closeOnExit: "graceful"`
  the tab closes on a clean `exit` and stays open on errors.
- **Restore** the original console mode on any exit path.

---

## 4. Keyword highlighting

### 4.1 Problems in the WinSerial approach

1. `std::regex_replace` runs on raw bytes, so a rule can match inside an existing escape sequence
   (e.g. `ESC[0;32m`) or inside a word already colored by an earlier pass, and break it.
2. It resets to default color after a match (`ESC[0m`), wiping the server's own colors (e.g. `ls --color`).
3. `std::regex` is slow and can overflow the stack on long lines.
4. Line buffering + 50 ms debounce adds lag to every keystroke echo.
5. Highlighting inside full-screen apps (`vim`, `htop`) corrupts their screen.

### 4.2 Design

- **VT-aware scan:** split the stream into escape sequences (pass through untouched) and text runs
  (eligible for highlighting).
- **Single pass, all rules at once:** compile rules into one matcher; on overlap, the earlier rule
  (higher priority) wins. Engine: PCRE2 with JIT (D5, benchmark in 4.4).
- **Save and restore color:** after a match, restore the server's current SGR state, not plain reset.
- **No lag:** write text right away. Only hold back a trailing partial word (bounded, e.g. ≤ 64 bytes
  or ≤ 10 ms) so a keyword split across two reads still matches.
- **Auto-off in full-screen apps:** pause highlighting while the alternate screen buffer is active
  (`ESC[?1049h` … `ESC[?1049l`).
- **Toggle at runtime** via hotkey.

### 4.3 User-defined rules (G5)

File: `%APPDATA%\WinSSH\highlight.json` (hot-reloaded on change). Example:

```json
{
  "ruleSets": {
    "default": [
      { "name": "errors",  "keywords": ["error", "failed", "fault", "denied"], "style": { "fg": "red", "bold": true } },
      { "name": "ok",      "keywords": ["ok", "success", "connected"],         "style": { "fg": "green" } },
      { "name": "ipv4",    "regex": "\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b",          "style": { "fg": "yellow" } },
      { "name": "mac",     "regex": "\\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\\b",   "style": { "fg": "yellow" } }
    ],
    "cb2-logs": [
      { "name": "fc-services", "regex": "\\bfc-[a-z]+\\b", "style": { "fg": "cyan" } },
      { "name": "journal-err", "regex": "\\b(ERR|CRIT|ALERT|EMERG)\\b", "caseSensitive": true, "style": { "fg": "white", "bg": "red" } }
    ]
  }
}
```

- Rule fields: `name`, `keywords` (whole-word, list) **or** `regex`, `caseSensitive` (default `false`),
  `style` (`fg`, `bg`: 16 named colors, `#rrggbb`, or 0–255; `bold`, `underline`, `italic`), `enabled`.
- A profile picks one or more rule sets (`"highlight": ["default", "cb2-logs"]`).
- Built-in `default` set = WinSerial's current keyword list, so behavior matches out of the box.
- `winssh --check-rules` validates the file and prints each rule's sample output in color.
- Bad rule → warning on start, that rule is skipped, session still opens.

### 4.4 Regex engine benchmark (D5, 2026-11-26)

Spike: `spike/regex/`. WinSerial's 6 default rules combined into one pattern, 8 MB of output
(synthetic journal-style lines plus captured sessions from the Ubuntu server and CB2), one match
loop per line like the highlighter does per text run.

| Engine | Speed | Matches |
|---|---|---|
| PCRE2 10.47, JIT | ~42 MB/s | same counts as RE2 for all 6 rules |
| RE2 | ~39 MB/s | (reference) |
| `std::regex` (WinSerial today) | ~0.4 MB/s | ~100× slower |

Bad user rule `(a+)+$` on 29 characters: PCRE2 stops at the match limit in 0.45 ms; RE2 is linear
(0.02 ms). Both are safe. PCRE2 wins on syntax (lookaround, backreferences) and dependencies
(RE2 needs abseil, a much longer build).

---

## 5. Profiles and history (G6)

### 5.1 Storage

| Data | Where |
|---|---|
| Profiles, settings | `%APPDATA%\WinSSH\profiles.json`, `settings.json` |
| Recent connections (MRU, last N=20) | `%APPDATA%\WinSSH\history.json` |
| Passwords / key passphrases | **Windows Credential Manager** (`CredWriteW`, target `WinSSH:<profile-id>`), never in JSON |
| Host keys | `%USERPROFILE%\.ssh\known_hosts` (shared with OpenSSH) |
| Session logs | `%LOCALAPPDATA%\WinSSH\logs\` |

### 5.2 Profile fields

```json
{
  "id": "cb2-lab",
  "name": "CB2 lab unit",
  "host": "192.168.1.50",
  "port": 22,
  "user": "root",
  "auth": "password | key | agent | keyboard-interactive",
  "identityFile": "~/.ssh/id_ed25519",
  "savePassword": true,
  "highlight": ["default", "cb2-logs"],
  "keepAliveSec": 30,
  "autoReconnect": true,
  "log": { "enabled": false, "timestamps": true, "stripAnsi": true },
  "hostKeyPolicy": "strict | accept-new | warn-on-change",
  "algorithms": { "hostKey": "+ssh-rsa" },
  "wtTab": { "color": "#2d7d46", "icon": null }
}
```

`hostKeyPolicy: warn-on-change` is for lab devices whose host key changes after every reflash;
default stays `strict`.

### 5.3 How the user picks a connection

```
winssh                         → TUI picker (profiles + recent), see 6.3
winssh user@host[:port]        → quick connect, added to history
winssh -p 2222 -l root host    → OpenSSH-style flags: -p port, -l user, -i keyfile
winssh cb2-lab                 → open a profile (a destination that matches a profile id)
winssh --save cb2-lab user@host → create profile from args
winssh --list                  → list profiles
winssh --import-ssh-config     → turn Host entries from ~/.ssh/config into profiles
```

---

## 6. UX

### 6.1 Escape key (command mode) — decided: `Ctrl+]` (D3)

`Ctrl+A` (WinSerial) collides with shells and tmux/screen. Default is `Ctrl+]` (like telnet),
configurable in `settings.json`. Optionally also support OpenSSH's `~.` style escapes after Enter (backlog).

| Keys | Action |
|---|---|
| `Ctrl+]`, `h` | Help |
| `Ctrl+]`, `f` | Toggle highlighting |
| `Ctrl+]`, `l` | Toggle session log to file |
| `Ctrl+]`, `t` | Toggle timestamp prefix on lines |
| `Ctrl+]`, `r` | Reconnect now |
| `Ctrl+]`, `i` | Connection info (host, cipher, host-key fingerprint, uptime) |
| `Ctrl+]`, `x` | Disconnect and exit |
| `Ctrl+]`, `Ctrl+]` | Send a literal `Ctrl+]` |

### 6.2 Connection status line

Print short colored status lines, not dialogs: `Connecting to cb2-lab (192.168.1.50:22)…`,
`Connection lost — reconnecting in 3 s (attempt 2)…`, `Press Ctrl+] h for help`.
Set the tab title to `WinSSH: <profile or user@host>`.

### 6.3 Picker UI — decided: TUI picker + Win32 profile dialog (D4)

**Flow**

```
New WT tab (profile "WinSSH")
  └─ TUI picker: Profiles + Recent + filter
       ├─ Enter on a profile / recent  → connect
       ├─ type user@host + Enter       → quick connect (added to Recent)
       ├─ n / e / s                    → Win32 dialog (new / edit / save recent as profile)
       │     ├─ Save & Connect → connect
       │     ├─ Save           → back to TUI (list updated)
       │     └─ Esc / Cancel   → back to TUI
       ├─ d                            → delete profile (confirm y/N)
       └─ Esc                          → exit
```

**TUI picker** — drawn inside the tab with VT sequences (like `fzf`):

```
 WinSSH — select a connection                      Filter: cb2_

  PROFILES
  > CB2 lab unit          root@192.168.1.50:22      [cb2-logs]
    CB2 test rig (HIL)    root@10.0.0.21:22         [cb2-logs]

  RECENT
    hai@build-server      2 hours ago
    admin@192.168.1.1     yesterday

  ↑/↓ move  Enter connect  type to filter  n new  e edit  s save recent  d delete  Esc quit
```

- Type-to-filter matches profile name, host, and user.
- Filter text that looks like `user@host[:port]` + Enter → quick connect, no dialog.
- Screen is cleared before the SSH session starts.
- Handles resize while open.

**Win32 profile dialog** — only for creating/editing profiles (many fields):

- Fields: name, host, port, user, auth type, identity file (with Browse…), "Save password" checkbox +
  password field, highlight rule sets, keepalive, auto-reconnect, host-key policy.
- Buttons: **Connect** (no save), **Save**, **Save & Connect**. Enter = Save & Connect, Esc = Cancel.
- Fully keyboard-driven (Tab order, mnemonics).
- `e` on a profile and `s` on a recent entry open the same dialog, pre-filled.
- Password is written to Credential Manager only; unchecking "Save password" deletes the stored one.
- **Owner and focus:** under Windows Terminal, `GetConsoleWindow()` returns a hidden pseudo window,
  so WinSerial's `CenterParentWindow` would center on the wrong window. Capture `GetForegroundWindow()`
  at startup (the WT window), use it as owner, center on it, bring the dialog to the foreground, and
  give focus back to WT when it closes.

Password (when not saved) and host-key prompts are always in the terminal, not in the dialog.

---

## 7. Windows Terminal integration (G3)

Use **WT JSON fragments** instead of editing `settings.json` with regex:

- Write `%LOCALAPPDATA%\Microsoft\Windows Terminal\Fragments\WinSSH\winssh.json`.
- One base profile `WinSSH` (opens the picker), plus **one WT profile per saved WinSSH profile**
  (`commandline: "winssh.exe -p cb2-lab"`) so each device shows up in the WT dropdown and in
  `wt -p "WinSSH: cb2-lab"`.
- WinSSH rewrites the fragment whenever profiles change. Uninstall deletes the folder.
- WT reads fragments on start; user's own `settings.json` is never touched.
- Use stable GUIDs per profile (derive from profile id) so WT keeps user overrides across rewrites.

---

## 8. More ideas (backlog, not v1)

Most useful for embedded / lab work first:

1. **Auto-reconnect** when the device reboots (after a flash or SWU update): wait, retry with backoff,
   reattach in the same tab.
2. **Session log to file** with optional timestamps and ANSI stripped — easy to attach to a Jira ticket.
3. **Timestamp prefix** on each output line (toggle).
4. **Send-on-connect commands** per profile (e.g. `journalctl -f -u fc-transaction`).
5. **Jump host / ProxyJump.**
6. **Local port forwarding** (`-L`), e.g. to reach the CB2 local portal (`fc-portal`).
7. **Broadcast input** to several sessions (multi-device test) — probably needs a different design.
8. **Search/filter mode** (pause output and show only lines matching a rule).
9. Shared core library with WinSerial; maybe WinSerial later reads the same `highlight.json`.

---

## 9. Security checklist

- No password, passphrase, or key material in JSON, Registry, logs, or command-line args.
- Credential Manager for secrets; user can opt out of saving.
- Host-key verification on by default; show fingerprint (SHA256) on first connect.
- Session logs may contain secrets typed at prompts → logging off by default; warn when turned on.
- Wipe password buffers after use (`SecureZeroMemory`).
- Signed releases later (code signing cert) to avoid SmartScreen warnings.

---

## 10. Implementation plan

Each milestone ends with something runnable.

### M0 — Spike and skeleton (decisions locked)

- [x] Spike: libssh vs libssh2 against a Linux dev server (table 3.2, run 1). CB2 / switch checks
      moved to M1.
- [x] Decide D2 (library): libssh2.
- [x] Repo skeleton: CMake + `CMakePresets.json` + vcpkg manifest (`x64-windows-static`, static CRT,
      single `.exe`), `src/` with `winssh_core` library + `WinSSH` app, `cmake/Version.h.in`,
      placeholder icon, VERSIONINFO, app manifest (UTF-8 code page, Common Controls v6, PerMonitorV2).
- [x] Unit tests: Catch2 v3 via CTest (`ctest --preset release`); first tests for CLI parsing and UTF-8.
- [x] GitHub Actions: `.github/workflows/ci.yml` builds Release on `windows-latest`, runs tests,
      smoke-tests `--version`, uploads `WinSSH.exe`. Green on PR #1 (2026-11-26). The preset has no
      fixed generator because `windows-latest` moved to Visual Studio 2026.

### M1 — Basic SSH session

- [ ] Verify libssh2 on a network switch (legacy algorithms), keyboard-interactive, passphrase key (CB2 done in spike run 2).
- [x] Host-key preference: `ssh-ed25519` first, then ECDSA, RSA-SHA2; legacy `ssh-rsa` (SHA-1) off by default.
- [x] CLI parsing: `[user@]host[:port]` (IPv6 in brackets), `-p port`, `-l user`, `-i keyfile`, `-V`, `-h` (done in M0 skeleton).
- [x] Auth: agent → key files (`-i` or `~/.ssh/id_ed25519`, `id_ecdsa`, `id_rsa`; passphrase prompt only for keys
      the server accepts) → keyboard-interactive → password (3 tries). Agent path tested on 2 hosts; password,
      keyboard-interactive and passphrase keys are implemented but not yet tested against a server.
- [x] Host-key check against `known_hosts` (same key type only; unparsable lines skipped), first-connect
      prompt with SHA256 fingerprint, accepted key appended as one line, mismatch refused with a warning.
      First-connect prompt not yet tested end to end (both test hosts are already known).
- [x] PTY + shell, raw console input, resize → `window-change`, UTF-8 (no split characters on output).
- [x] Clean exit, remote exit code (255 on connection/auth errors), console mode restored; Ctrl+C while
      connecting or at a prompt stops cleanly instead of killing the process.
- [x] End-to-end test driver `tools/conpty-run` + scripts in `tests/e2e/` (`run.ps1 -Target user@host`).
      `basic-session.txt` passes on the Ubuntu server and CB2 E13F8: PTY size, resize, UTF-8 both ways,
      Ctrl+C, `less` (alternate screen), exit code. `password-cancel.txt` and `connect-refused.txt` pass.
- [ ] Manual check in Windows Terminal: `vim`, `htop`, tab completion, mouse, paste, closing the tab.
- **Done when:** `vim`, `htop`, `less`, tab completion, `Ctrl+C` all behave like `ssh.exe` in WT.

**M1 design**

```
main thread (boost::asio io_context)                 input thread
  tcp socket ── libssh2 session (non-blocking)        ReadConsoleInputW
     ▲  async_wait(read/write) per                      ├─ KEY_EVENT chars (VT input mode) → UTF-8
     │  libssh2_session_block_directions()              └─ WINDOW_BUFFER_SIZE_EVENT → new size
  Bridge::Pump()                                       post() to io_context ──┐
     ├─ channel_read(stdout/stderr) → console (UTF-8 complete sequences only)  │
     ├─ pending window-change → channel_request_pty_size                        │
     └─ pending input → channel_write  ◄────────────────────────────────────────┘
```

- `src/console/`: `ConsoleModeGuard` (save/restore modes and code pages on every exit path),
  raw VT input, `InputReader` thread, `OutputWriter` (holds back an incomplete UTF-8 tail),
  line prompt with echo off for passwords and host-key questions.
- `src/ssh/`: `Session` (TCP connect with timeout, handshake, host-key preference),
  `KnownHosts` (check; append one line on accept instead of rewriting the file),
  `Auth` (agent → key files with passphrase prompt → keyboard-interactive → password, like OpenSSH),
  default key files `~/.ssh/id_ed25519`, `id_ecdsa`, `id_rsa`.
- `src/app/`: `Bridge` (the pump loop above), `RunConnect()` (orchestration, exit codes:
  remote status, 255 on connection/auth errors like `ssh`).
- Handshake and auth run in libssh2 blocking mode (simple, prompts are interactive anyway);
  the session switches to non-blocking only for the interactive loop.
- `Ctrl+C` is sent to the server as `0x03`: `ENABLE_PROCESSED_INPUT` off plus a console control
  handler that ignores `CTRL_C_EVENT` / `CTRL_BREAK_EVENT`.

### M2 — Highlighting engine

- [x] VT scanner (`highlight/VtScanner`): text vs CSI / OSC / DCS / two-byte sequences, reassembles
      sequences split across reads, classifies SGR and alternate-screen switches.
- [x] Rule engine (`highlight/Rules`, `highlight/Highlighter`): all rules in one PCRE2 pattern (user
      groups mapped per rule), earlier rule wins, server SGR replayed after each match, match limit
      against bad regexes, invalid UTF-8 tolerated. Hold-back: a partial line ≤ 64 bytes is held
      whole (multi-word keywords still match), longer lines hold only the last word; flushed after 10 ms.
- [x] Alternate-screen auto-pause.
- [x] Built-in `default` rule set (WinSerial keywords and colors).
- [x] Escape key command mode `Ctrl+]`: `h` help, `f` toggle highlight, `i` connection info, `x` exit
      (exit code 0), `Ctrl+]` twice sends it. `--no-highlight` flag.
- [x] Tests: unit tests for scanner, highlighter, rules, escape keys (48 total); e2e `highlight.txt`
      passes on the Ubuntu server and CB2 (colors on, off, on again; `Ctrl+]` commands).
- [ ] Manual check in Windows Terminal: no visible typing lag, `ls --color` / `ip -c a` colors kept.
- **Done when:** highlighted output never breaks server colors or full-screen apps; no visible typing lag.

### M3 — Custom highlight rules (G5)

- [ ] `highlight.json` schema, loader, validation, error messages.
- [ ] Hot reload on file change.
- [ ] Named rule sets, per-session selection (`--highlight default,cb2-logs`).
- [ ] `winssh --check-rules` preview.
- [ ] Docs: rule format with examples.

### M4 — Profiles and history (G6)

- [ ] `profiles.json`, `history.json`, `settings.json` load/save with schema version.
- [ ] Credential Manager wrapper (save / read / delete password per profile).
- [ ] CLI: `-p`, `--save`, `--list`, `--import-ssh-config`.
- [ ] TUI picker: Profiles + Recent sections, type-to-filter, keys `Enter`/`n`/`e`/`s`/`d`/`Esc`, resize.
- [ ] Quick connect from the filter box (`user@host[:port]` + Enter), added to Recent.
- [ ] Win32 profile dialog: fields, Browse for key file, Connect / Save / Save & Connect, keyboard-only use.
- [ ] Dialog owner/centering/focus under Windows Terminal (`GetForegroundWindow()` captured at start).
- [ ] Delete profile with confirm; also removes its Credential Manager entry.
- [ ] Per-profile options: highlight sets, keepalive, host-key policy, legacy algorithms.
- **Done when:** a new WinSSH tab → pick or create a profile → connected, without touching the mouse.

### M5 — Windows Terminal integration and release (G3)

- [ ] Fragment generator: base profile + one per saved profile; rewrite on change.
- [ ] Tab title.
- [ ] Inno Setup installer (PATH, fragment, Start Menu), portable zip.
- [ ] Release workflow: tag `vX.Y.Z` → build, test, attach installer + zip.
- [ ] README with screenshots/GIF.

### M6 — Lab-work extras

- [ ] Keepalive + auto-reconnect with backoff.
- [ ] Session logging (+ hotkey), timestamp prefix.
- [ ] Send-on-connect commands.
- [ ] Then pick from backlog (section 8).

### 10.1 Branching and releases (D9)

| Branch | Created from | Merges into | How |
|---|---|---|---|
| `feature/*`, `fix/*` | `develop` | `develop` | Pull request, CI green; squash or merge commit |
| `develop` | — | `main` | At release, **merge commit only** (squashing would make `main` and `develop` diverge) |
| `hotfix/*` | `main` | `main`, then back into `develop` | Pull request, CI green |

- `main` always equals the latest release. Each release is a tag `vX.Y.Z` on `main`; the release
  workflow (M5) runs on the tag, not on every push to `main`.
- Stacked feature branches (one started from another before it merged): after the first PR is
  squash-merged, rebase the next one with `git rebase --onto develop <old-base-branch>`.
- Recommended GitHub settings: protect `main` and `develop` (pull requests only, CI must pass).

### Testing approach

- Unit tests: VT tokenizer, rule engine (overlap, split chunks, UTF-8, SGR restore), config parsing, CLI parsing.
- Integration: local OpenSSH server (Windows optional feature or a Linux container/VM) with password and key users; scripted connect/run/exit.
- Manual checklist per release: `vim`, `htop`, `tmux`, resize, long `dmesg`/`journalctl` output, real CB2 unit, reconnect after reboot.

---

## 11. Decisions log

| # | Topic | Decision | Date |
|---|---|---|---|
| D1 | In-process library (A) vs wrap `ssh.exe` (B) | **A** — in-process library; B only as fallback | 2026-11-26 |
| D2 | libssh vs libssh2 | **libssh2** — Windows OpenSSH agent works, BSD-3 license (spike run 1, section 3.2) | 2026-11-26 |
| D3 | Escape key | **`Ctrl+]`**, configurable | 2026-11-26 |
| D4 | Picker UI | **TUI picker** for choosing; **Win32 dialog** only for new/edit profile (section 6.3) | 2026-11-26 |
| D5 | Regex engine | **PCRE2 (JIT)**: as fast as RE2, full Perl syntax, no abseil dependency (4.4) | 2026-11-26 |
| D6 | Config format | JSON in `%APPDATA%\WinSSH\` | |
| D7 | Secret storage | Windows Credential Manager | |
| D8 | License | Repo has MIT; WinSerial is Apache-2.0. Check compatibility if code is copied from WinSerial | |
| D9 | Branching | `feature/*` → `develop` via PR; `develop` → `main` at release (see 10.1) | 2026-11-26 |
