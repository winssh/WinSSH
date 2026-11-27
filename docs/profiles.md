# Profiles and history

A profile is a saved connection you open by name: `winssh cb2-lab`.

```text
%APPDATA%\WinSSH\profiles.json    profiles
%APPDATA%\WinSSH\history.json     recent connections (last 20)
Windows Credential Manager        saved passwords ("WinSSH:<profile-id>")
```

(`%WINSSH_CONFIG_DIR%` replaces `%APPDATA%\WinSSH` when set.)

## The picker

`winssh` with no destination opens the connection picker in the terminal:

```text
 WinSSH — select a connection                                  Filter: cb2_

  PROFILES
  > CB2 lab unit        root@10.0.0.108       [default,journal]
  RECENT
    admin@192.168.1.1   2026-11-26T10:00:00Z

 ↑/↓ move  Enter connect  type to filter  Ctrl+N new  Ctrl+E edit  Ctrl+S save recent  Del delete  Esc quit
```

| Key | Action |
|---|---|
| typing | Filter (all words must match name, address, id or rule sets) |
| `↑` `↓` `PgUp` `PgDn` `Home` `End` | Move |
| `Enter` | Connect to the selection; if the filter is `user@host[:port]`, connect to that host |
| `Ctrl+N` | New profile (pre-filled when the filter is `user@host`) |
| `Ctrl+E` or `F2` | Edit the selected profile |
| `Ctrl+S` | Save the selected recent connection as a profile |
| `Delete` | Delete the selected profile (asks `y/N`; also removes its saved password) |
| `Backspace`, `Ctrl+U` | Delete a character / clear the filter |
| `Esc` | Clear the filter; on an empty filter, quit |

### Profile dialog

`Ctrl+N`, `Ctrl+E` and `Ctrl+S` open a small window centered on the terminal. Everything works
with the keyboard: `Tab` moves between fields, underlined letters are `Alt` shortcuts, `Enter`
is **Save & Connect**, `Esc` cancels.

- **Connect** uses the values once without saving them (a password typed here is used once).
- **Save** stores the profile and goes back to the picker.
- **Save password** keeps the typed password in Windows Credential Manager. Leave the password field
  empty to keep a password that is already saved; untick the box to remove it.
- Leaving **Profile ID** empty and typing a host fills in an ID from the host name.

## Commands

```powershell
winssh --save cb2-lab root@10.0.0.108                  # create or update a profile
winssh --save cb2-lab --highlight default,journal --keepalive 30 --host-key-policy warn-on-change root@10.0.0.108
winssh --save switch1 --save-password --legacy-algorithms admin@192.168.1.1
winssh cb2-lab                                          # connect
winssh -l fc -p 2222 cb2-lab                            # flags override the profile for this connection
winssh --list                                           # profiles and recent connections
winssh --delete cb2-lab                                 # also removes its saved password
winssh --import-ssh-config                              # add profiles from ~/.ssh/config
```

A destination without `@`, `:` or `[` that matches a profile id (not case-sensitive) opens the
profile. Anything else is a host name: `root@cb2-lab` connects to the host `cb2-lab`.

`--save` again with the same id updates the host, port and user, and changes only the options you
give; other fields stay as they are.

## profiles.json

```jsonc
{
  "version": 1,
  "profiles": [
    {
      "id": "cb2-lab",                // what you type: winssh cb2-lab
      "name": "CB2 lab unit",         // display name (default: id)
      "host": "10.0.0.108",
      "port": 22,
      "user": "root",
      "identityFile": "~/.ssh/fc_ed25519",
      "savePassword": true,
      "highlight": ["default", "journal"],
      "keepAliveSec": 30,
      "hostKeyPolicy": "warn-on-change",
      "legacyAlgorithms": false,
      "autoReconnect": true,
      "log": { "enabled": true, "timestamps": true },
      "screenTimestamps": false
    }
  ]
}
```

| Field | Default | Meaning |
|---|---|---|
| `id` | required | 1-64 letters, digits, `.`, `_`, `-` |
| `name` | `id` | Shown in the tab title and lists |
| `host` | required | Host name or IP address |
| `port` | `22` | |
| `user` | your Windows user name | |
| `identityFile` | `~/.ssh/id_ed25519`, `id_ecdsa`, `id_rsa` | Private key file (`~` = your profile folder) |
| `savePassword` | `false` | Keep the password in Windows Credential Manager (see below) |
| `highlight` | `["default"]` | Highlight rule sets ([highlight-rules.md](highlight-rules.md)) |
| `keepAliveSec` | `0` (off) | Send an SSH keepalive when the connection was idle this long |
| `hostKeyPolicy` | `strict` | See below |
| `legacyAlgorithms` | `false` | Also offer `ssh-rsa`/`ssh-dss` host keys, SHA-1 key exchange and CBC ciphers, for old switches and routers |
| `autoReconnect` | `false` | Reconnect in the same tab when the connection drops (see below) |
| `log` | off | `{ "enabled": true }` logs every session of this profile to a file; `"timestamps": false` leaves out the times in the file (see below) |
| `screenTimestamps` | `false` | Show the time at the start of each output line on screen (see below) |

Comments are allowed. Fields WinSSH does not know are kept when it rewrites the file. If the file
has errors, `--save`, `--delete` and `--import-ssh-config` refuse to change it, so nothing is lost.

## Automatic reconnect

With `autoReconnect: true` (or `--reconnect`, or the dialog's **Reconnect automatically** box),
WinSSH stays open when the connection drops, for example while a device reboots after flashing:

```text
[winssh] connection to 10.0.0.108: Connection reset by peer
[winssh] reconnecting to root@10.0.0.108 in 4 s (attempt 3) - Enter: now, Esc: stop
[winssh] reconnected to root@10.0.0.108
```

- Retries wait 1, 2, 4, 8, then 10 s, until the host answers. `Enter` retries now; `Esc`, `q` or
  `Ctrl+C` stops (exit code 255).
- It also waits for a host that is not up yet when WinSSH starts.
- Not retried: a normal `exit` (WinSSH ends with the shell's exit code), `Ctrl+] x`, a failed login,
  or a changed host key (a person has to decide; see `warn-on-change` below).
- A shell killed by a signal (e.g. during shutdown) counts as a drop and is reconnected.
- If the connection dropped inside vim, less or htop, the terminal is put back to normal first.
- Reconnecting sessions send a keepalive every 15 s when `keepAliveSec` is not set, so a device that
  disappears without closing the connection is noticed. `--keepalive 0` turns that off.
- Without automatic reconnect, `Ctrl+] r` still reconnects once, on request.

## Session log and timestamps

`Ctrl+]` then `l` starts or stops a log file; `--log` (or the profile's `"log": { "enabled": true }`,
or the dialog's **Log the session to a file** box) starts one with the session. Files go to
`%LOCALAPPDATA%\WinSSH\logs`, named `<profile or host>-YYYYMMDD-HHMMSS.log`:

```text
# WinSSH session log: root@10.0.0.115 (profile cb2), started 2026-11-27 17:17:41
[17:17:41.779] root@fuelcloud-E13F8:~# journalctl -f -u fc-transaction
[17:17:42.101] Nov 27 17:17:42 fuelcloud-E13F8 fc-transaction[812]: ...
[17:17:43.120] --- connection to 10.0.0.115: Connection reset by peer ---
[17:17:52.513] --- reconnected to root@10.0.0.115 ---
# log closed 2026-11-27 17:20:05
```

- Only what the **server sends** is logged, as plain text: no colors or other escape sequences,
  progress lines keep their last state. What you type appears only where the server echoes it
  (a shell does; password prompts do not).
- Each line starts with the time its first character arrived (see the table below to turn it off).
- Full-screen apps (vim, less, htop) are not logged; one line notes when they open and close.
- A drop and the reconnect are marked; the same file continues.
- `Ctrl+] i` shows the current log file.

### Timestamps: on screen and in the log file

Two separate settings:

| | Timestamps on screen | Timestamps in the log file |
|---|---|---|
| What | Gray `[HH:MM:SS.mmm]` at the start of each output line in the terminal (paused in full-screen apps), e.g. to see how long a boot step takes | `[HH:MM:SS.mmm]` at the start of each line of the log file |
| Default | **off** | **on** |
| Profile | `"screenTimestamps": true` | `"log": { "timestamps": false }` to turn off |
| Dialog | **Show timestamps on screen** | **with times on each line** (next to **Log the session to a file**) |
| Command line | `--timestamps` | `--log-no-timestamps` |
| In a session | `Ctrl+]` then `t` | (fixed for the log file once it starts) |

`--save` stores these flags in the profile like the other options.

## Saved passwords

With `savePassword: true` (or `--save-password`):

1. The first time password or keyboard-interactive login works, the typed password is saved in
   Windows Credential Manager as `WinSSH:<profile-id>`. It is never written to a file.
2. Next time WinSSH tries the saved password first. If the server rejects it (the password changed),
   WinSSH asks again and saves the new one.
3. `winssh --delete <id>` removes it. You can also remove it in Control Panel → Credential Manager →
   Windows Credentials.

Key and agent login are always tried before a password.

## Host key policy

| Policy | Unknown host | Changed host key |
|---|---|---|
| `strict` (default) | Ask, like OpenSSH | Refuse with a warning |
| `accept-new` | Add without asking | Refuse with a warning |
| `warn-on-change` | Ask | Warn, ask "Trust the new key and continue?", and add it |

`warn-on-change` is meant for lab devices that get a new host key on every reflash. Use it only for
devices on a network you trust. Accepted keys are appended to `~/.ssh/known_hosts` (shared with
OpenSSH); WinSSH never rewrites existing lines.

## Importing ~/.ssh/config

`--import-ssh-config` adds one profile per concrete `Host` alias (patterns with `*`, `?`, `!` are not
profiles) using `HostName` (with `%h`), `User`, `Port` and `IdentityFile`. Values from earlier
matching blocks win, as in OpenSSH, so `Host *` defaults at the end fill in what is missing.
`Match` blocks and `Include` are not supported. Existing profile ids are skipped.
