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

File: `%APPDATA%\WinSSH\highlight.json` (hot-reloaded on change). Implemented in M3; the user
reference is `docs/highlight-rules.md`. Example:

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
  "identityFile": "~/.ssh/id_ed25519",
  "savePassword": true,
  "highlight": ["default", "cb2-logs"],
  "keepAliveSec": 30,
  "autoReconnect": true,
  "log": { "enabled": false, "timestamps": true, "stripAnsi": true },
  "hostKeyPolicy": "strict | accept-new | warn-on-change",
  "legacyAlgorithms": false,
  "wtTab": { "color": "#2d7d46", "icon": null }
}
```

Implemented in M4 PR 1 (user reference: `docs/profiles.md`): everything except `autoReconnect`, `log`
(M6) and `wtTab` (M5), which are kept in the file but not used yet. An `auth` field was dropped: the
OpenSSH order (agent, keys, keyboard-interactive, password) covers it. The planned `algorithms`
object became one `legacyAlgorithms` switch.

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
       ├─ Ctrl+N / Ctrl+E / Ctrl+S     → Win32 dialog (new / edit / save recent as profile)
       │     ├─ Save & Connect → connect
       │     ├─ Save           → back to TUI (list updated)
       │     └─ Esc / Cancel   → back to TUI
       ├─ Delete                       → delete profile (confirm y/N)
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

  ↑/↓ move  Enter connect  type to filter  Ctrl+N new  Ctrl+E edit  Ctrl+S save recent  Del delete  Esc quit
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
- `Ctrl+E` on a profile and `Ctrl+S` on a recent entry open the same dialog, pre-filled. (Plain letters
  were planned first, but letters must go to the filter.)
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
  `wt -p "WinSSH: cb2-lab"`. (Implemented in M5 with `winssh.exe cb2-lab`, see `wt/Fragment`.)
- WinSSH rewrites the fragment whenever profiles change (once installed with `--wt-install` or the
  installer). Uninstall deletes it.
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
      First-connect prompt tested end to end in M6 (`hostkey-first.txt`, CB2's new address): wrong
      answer asked again, `yes` appends an `ssh-ed25519` line that `ssh.exe` then accepts without a prompt.
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
- [x] Manual check in Windows Terminal on CB2 (2026-11-26): `ls --color` and `ip -c a` colors kept, no
      broken sequences, `LOWER_UP` not matched (whole words only). Typing lag: not reported.
- **Done when:** highlighted output never breaks server colors or full-screen apps; no visible typing lag.

### M3 — Custom highlight rules (G5)

- [x] `highlight.json` loader (`config/HighlightConfig`, nlohmann-json): comments allowed, `style`
      as a color string or an object, located errors (`ruleSets.cb2[1] ('fc'): ...`), bad rules
      skipped, unknown fields as warnings, invalid JSON falls back to built-in rules, UTF-8 BOM ok.
      Settings folder `%APPDATA%\WinSSH`, overridable with `WINSSH_CONFIG_DIR`.
- [x] Hot reload: the session checks the file every second (`app/RulesSource`), swaps the rules and
      prints `[winssh] highlight rules reloaded: N rules` with any errors.
- [x] Named rule sets; a file `default` replaces the built-in one; `--highlight a,b` (earlier set wins).
- [x] `winssh --check-rules` (color preview, exit 1 on problems), `--test-rules "<text>"`,
      `--init-rules` (commented starter file with `default` and a `journal` example set).
- [x] Docs: `docs/highlight-rules.md`.
- [x] Tests: config/CLI/reload unit tests (60 total); e2e `custom-rules.txt` (file `default` replaces
      built-ins, reload during the session) passes on the Ubuntu server and CB2.

### M4 — Profiles and history (G6)

**PR 1 — storage and CLI (done):**

- [x] `profiles.json` / `history.json` (`config/Profiles`): `version` field, comments allowed, located
      errors, case-insensitive unique ids, unknown fields kept on save, atomic writes (`.tmp` +
      replace). `--save` / `--delete` / import refuse to rewrite a file that has errors.
      `settings.json` not needed yet (escape key stays `Ctrl+]`).
- [x] Password vault (`vault/PasswordVault`, Windows Credential Manager, `WinSSH:<id>`): saved after the
      first successful password / keyboard-interactive login, tried first next time, asked again and
      replaced when rejected, removed by `--delete`. (Module is not called `credentials/` because the
      dev machine's permission rules block paths containing that word.)
- [x] CLI: `winssh <profile>` (destination without `@`, `:`, `[`), flags override profile values,
      `--save <id> [--save-password]`, `--list` (profiles + recent), `--delete <id>`,
      `--import-ssh-config` (Host aliases, wildcards, first value wins; no Match / Include).
- [x] Per-profile options (also as flags): highlight sets, `--keepalive` (libssh2 keepalive timer),
      `--host-key-policy strict|accept-new|warn-on-change`, `--legacy-algorithms`. History recorded
      after the shell starts; tab title = profile name.
- [x] Tests: 73 unit tests (profiles, history, ssh config import, vault with a test-only prefix,
      plan resolution, CLI); e2e `profile-session.txt` (`run.ps1 -AsProfile`) passes on CB2.
      Not tested against a real server yet: saved-password login, `--legacy-algorithms` (needs an old
      switch), `warn-on-change` with a changed key.

**PR 2 — picker and dialog:**

- [x] TUI picker (`picker/PickerModel`, `PickerView`, `PickerLoop`): Profiles + Recent sections,
      type-to-filter (all words, any field), ↑/↓/PgUp/PgDn/Home/End, scrolling, resize, alternate
      screen. **Keys changed from the plan:** `Ctrl+N` new, `Ctrl+E`/`F2` edit, `Ctrl+S` save a recent
      entry, `Delete` delete (confirm `y`), `Esc` clears the filter then quits. Plain `n`/`e`/`s`/`d`
      would collide with typing a filter ("dev" would delete).
- [x] Quick connect from the filter box (`user@host[:port]` + Enter), recorded in Recent. `Ctrl+N` after
      typing `user@host` pre-fills the dialog. Keys typed right after Enter go to the session.
- [x] Win32 profile dialog (`picker/ProfileDialog`, `IDD_PROFILE`): ID, name, host, port, user, key file
      with Browse, save password + password, highlight sets, keepalive, host-key policy, legacy
      algorithms; Connect / Save / Save & Connect (default) / Cancel; mnemonics and Tab order;
      validation shown in the dialog (`picker/ProfileForm`, unit tested); ID suggested from the host;
      rename keeps the saved password.
- [x] Dialog placement: `GetForegroundWindow()` captured at the start of `main`; dialog centered on it
      (inside the monitor's work area), brought to the front with `AttachThreadInput`, no
      cross-process owner; focus returned to the terminal on close.
- [x] Delete from the picker with confirm; removes the saved password too.
- [x] Tests: 91 unit tests (picker model, view, form); e2e `picker.txt` (filter, no match, Esc, delete
      confirm/cancel, connect) and `picker-quick.txt` pass on CB2 (`run.ps1 -Picker [-NoProfile]`).
      The dialog itself is a real window: manual test only. Manual test in Windows Terminal
      (2026-11-26): Ctrl+N dialog on top and centered, focus back to the terminal; Tab / Alt keys,
      Browse, Save & Connect; Ctrl+E edit; Delete.
- **Done when:** a new WinSSH tab → pick or create a profile → connected, without touching the mouse.

### M5 — Windows Terminal integration and release (G3)

- [x] Fragment generator (`wt/Fragment`): base `WinSSH` profile (picker) + `WinSSH: <name>` per profile,
      `commandline` = this exe + profile id, exe as icon, `tabColor` from a profile's `wtTab.color`.
      GUIDs are UUIDv5 of the profile id (case-insensitive) in a fixed WinSSH namespace.
      `--wt-install` / `--wt-uninstall`; after `--wt-install` (or the installer) the fragment is
      rewritten whenever profiles change (`--save`, `--delete`, import, picker, dialog). Not written
      unless installed, so a portable copy never touches WT by itself. `WINSSH_WT_FRAGMENTS_DIR` for tests.
- [x] Tab title (M4: `WinSSH: <profile name>` / `user@host`).
- [x] Inno Setup installer (`installer/WinSSH.iss`): per-user (no admin) in `%LOCALAPPDATA%\Programs\WinSSH`,
      optional user PATH (kept as REG_EXPAND_SZ), optional WT profiles (`--wt-install`, removed on
      uninstall), Start Menu entries, "open in Windows Terminal" at the end. Settings and saved
      passwords are kept on uninstall. `scripts/package.ps1` builds the portable zip and the installer
      and checks the exe's version.
- [x] Version from the tag (D10): `-DWINSSH_VERSION=X.Y.Z[-suffix]`, local builds `0.0.0-dev`.
- [x] Release workflow (`.github/workflows/release.yml`): tag `vX.Y.Z` on `main` → build with that
      version, test, package, `SHA256SUMS.txt`, GitHub Release (`-rcN` tags are pre-releases).
      Refuses tags that are not on `main`.
- [x] Tested locally: 95 unit tests; `--wt-install` / auto-sync / `--wt-uninstall` in temp folders;
      a `0.2.0-rc1` build packaged into zip + installer. Manual (2026-11-26): `WinSSH-setup-0.2.0-rc1.exe`
      installed and run OK; uninstall removed the app folder, the PATH entry (other entries intact) and
      the WT fragment, and kept `%APPDATA%\WinSSH`. Not yet: the release workflow on GitHub (needs a
      tag on `main`).
- [ ] README with screenshots/GIF.

### M5.1 — Release tagging (D11)

- [x] `scripts/tag.ps1` + `scripts/TagVersion.psm1`: with no options, the next version comes from the
      Conventional Commits since the last release (`feat:` → minor; `type!:` / `BREAKING CHANGE` → major,
      minor while on 0.x; anything else → patch; merge commits ignored). `-Rc` gives the next release
      candidate of that version (`rc10` sorts after `rc9`); without `-Rc` the result is the release itself,
      which also finalizes existing rcs. `-Bump major|minor|patch` and `-Version X.Y.Z[-rcN]` override.
      `-DryRun` shows the tag and the reason; without `-Push` the annotated tag stays local.
- [x] Checks: the commit is on `<remote>/main` (D9), the tag is new and newer than the last release,
      the commit's CI check runs passed (via `gh`, `-SkipCiCheck` to override).
- [x] `.github/workflows/tag.yml` (workflow_dispatch: dry_run default on, rc, bump, version): runs
      `tag.ps1 -Push`, then calls `release.yml` through `workflow_call`. Needed because tags pushed with
      `GITHUB_TOKEN` do not trigger workflows; it also means no double release. `release.yml` still runs
      on pushed tags (local `tag.ps1 -Push`), and takes the tag from `inputs.tag` when called.
      Inputs reach the script through environment variables (no script injection).
- [x] Tests: `tests/scripts/TagVersion.Tests.ps1` (42 checks, plain PowerShell, no Pester 5 needed),
      run by CTest as `tag-version-rules`. Local dry runs: `main` (only the initial commit) → `v0.0.1`;
      a commit not on `main` is refused; `develop`'s commits would give `v0.1.0` / `v0.1.0-rc1`.
      Not yet run on GitHub: `tag.yml` (works once it is on `main`).
- [x] Follow-up after the first release: a dry run reports the CI state (with a warning) instead of
      stopping; only creating a tag refuses non-green CI. CI runs are grouped by name
      ("Build and test (2 runs): in progress", `Get-CiSummary`, unit tested). Errors are one line
      (`tag.ps1: ...`, exit 1) instead of PowerShell's error record.

### First release log

| Tag | Date | Result |
|---|---|---|
| `v0.1.0-rc1` | 2026-11-27 | Failed at Configure: PowerShell split the unquoted `-DWINSSH_VERSION=0.1.0-rc1` at the first `.`. Fixed in #11. No release. |
| `v0.1.0-rc2` | 2026-11-27 | Failed at Package: #11 broke `package.ps1` (quotes inside a double-quoted string). Running the whole step locally then showed `SHA256SUMS.txt` was never written (`Get-ChildItem -Exclude`). Fixed in #12. No release. |
| `v0.1.0-rc3` | 2026-11-27 | **Published** (pre-release): installer, portable zip, `SHA256SUMS.txt`. |

Lessons:
- Test release steps in **PowerShell, exactly as the workflow runs them**. Local packaging was run
  from Git Bash, which does not split arguments, so the rc1 bug only showed on the runner.
- After a fix, re-run the whole failing step, not only the changed line (rc2).
- `powershell-scripts-parse` (CTest) now parses every `.ps1`/`.psm1`, so syntax errors fail CI on the PR.
- Failed rc tags are kept (no tag deletion); the next rc number is free.

### M6 — Lab-work extras

- [x] Auto-reconnect (PR M6-1): `autoReconnect` profile field (was kept as an unknown field since M4),
      `--reconnect` / `--no-reconnect`, dialog checkbox. `RunConnectPlan` is a loop over `RunAttempt`:
      connect/handshake errors, a dropped connection, a channel that does not close cleanly, or a shell
      ended by a signal → wait 1, 2, 4, 8, 10… s (`Enter` now, `Esc`/`q`/`Ctrl+C` stop, exit 255) and
      try again, also for the first connection. Never retried: normal exit (shell's code), `Ctrl+] x`,
      login failure, host-key failure. After a drop the terminal is reset (alternate screen, cursor,
      mouse modes) and the highlighter state is cleared. `Ctrl+] r` reconnects on request (also without
      auto-reconnect). Keepalive defaults to 15 s for reconnecting sessions so half-open connections
      are noticed. Tests: 104 unit tests; e2e `reconnect-stop.txt` (unreachable host, backoff to attempt 3,
      Esc → 255) passes; without `--reconnect` an unreachable host still exits 255 at once.
      `reconnect.txt` passes on CB2 (now 10.0.0.115): `kill -9 $PPID` while `less` was used → terminal
      leaves the alternate screen, reconnects, highlighting works, `Ctrl+] i` shows "reconnect 1",
      `Ctrl+] r` reconnects, `exit 6` → exit code 6 (found and fixed: the reconnect number was off by one).
      All 9 e2e scripts pass on CB2. e2e scripts now use `${TARGET}` / `${HOST}` placeholders (run.ps1).
      Manual test in Windows Terminal (reboot of CB2) found a display bug, fixed: after a drop at the
      prompt the "[winssh] connection to ..." line appeared at the top of the screen and the new session
      overwrote older lines. Cause: the reset always sent "leave alternate screen" (`?1049l`), which also
      restores the cursor saved on entering it; now sent only when the session was in the alternate
      screen (tracked by the highlighter, or by `Bridge` itself with `--no-highlight`). `reconnect.txt`
      now drops once inside `less` (expects `?1049l` from WinSSH) and twice at the prompt (rejects it,
      new conpty-run commands `mark` / `reject-raw`).
      Manual re-test (2026-11-27): CB2 rebooted during a session → the message appears under the last
      line, the session reconnects and continues below it.
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
- The version is the tag (D10): `git tag v0.2.0` on `main` → the workflow builds WinSSH 0.2.0. No
  version-bump commits; local builds report `0.0.0-dev`. Release candidates: `v0.2.0-rc1`.
- Tags are made with `scripts/tag.ps1` (locally) or the **Tag release** workflow (D11, M5.1).
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
| D10 | Versioning | From the release tag (`vX.Y.Z[-rcN]`), passed to CMake by CI; no bump commits | 2026-11-26 |
| D11 | Tagging | `scripts/tag.ps1`: next version from Conventional Commits (or `-Bump`/`-Version`), `-Rc`, `-DryRun`; local or via `tag.yml` dispatch, which calls `release.yml` | 2026-11-26 |
