# WinSSH

![WinSSH icon](assets/icons/WinSSH.png)

**SSH terminal for Windows, built to run as a Windows Terminal tab, with keyword highlighting you can configure.**

> Status: early development (milestone M5). Interactive SSH sessions, keyword highlighting with
> custom rules, profiles, the connection picker, Windows Terminal integration and an installer.
> See [docs/PLAN.md](docs/PLAN.md) for goals, design and milestones.

## Usage

```text
winssh                          Connection picker (profiles, recent, quick connect)
winssh [options] [user@]host[:port]
winssh [options] <profile>

  -p <port>               Port (default 22)
  -l <user>               User name
  -i <keyfile>            Private key file
  --highlight <sets>      Highlight rule sets, comma separated (default: default)
  --no-highlight          Turn off keyword highlighting
  --keepalive <sec>       Send a keepalive every <sec> seconds (0 = off)
  --host-key-policy <p>   strict (default), accept-new, or warn-on-change
  --legacy-algorithms     Allow ssh-rsa and SHA-1 key exchange (old devices)
  -V, --version           Show version
  -h, --help              Show this help
```

### Profiles

```powershell
winssh --save cb2-lab --highlight default,journal root@10.0.0.108   # save
winssh cb2-lab                                                       # connect
winssh --list                                                        # profiles and recent connections
winssh --import-ssh-config                                           # from ~/.ssh/config
```

Passwords can be saved in Windows Credential Manager (`--save-password`), never in a file.

Run `winssh` with no destination for the **picker**: type to filter, `Enter` to connect, or type
`user@host` and press `Enter`. `Ctrl+N` / `Ctrl+E` open a profile dialog, `Delete` removes a profile.
See [docs/profiles.md](docs/profiles.md).

### In a session

Press `Ctrl+]`, then:

| Key | Action |
|---|---|
| `h` | Help |
| `f` | Toggle keyword highlighting |
| `i` | Connection info (server, host key fingerprint, cipher) |
| `x` | Disconnect and exit |
| `Ctrl+]` | Send `Ctrl+]` to the server |

### Highlighting

Output is scanned for keywords and colored without touching the server's own escape sequences
or colors. Highlighting pauses while a full-screen app (vim, less, htop) is open. Default rules
(case-insensitive, whole words), the same as WinSerial:

| Color | Matches |
|---|---|
| Red | `error`, `failed`, `no`, `down`, `disabled`, `unknown`, `fault`, `shutdown`, `disconnected`, `denied`, `refused`, `problem`, `failure` |
| Green | `enabled`, `connected`, `up`, `yes`, `ok`, `success` |
| Yellow | IPv4 addresses, MAC addresses, `localhost`, `vlan` |
| Cyan | `ip address`, `show`, `interface`, `route`, `spanning-tree` |

### Custom rules

Put your own rule sets in `%APPDATA%\WinSSH\highlight.json`; WinSSH reloads the file while a
session is open. See [docs/highlight-rules.md](docs/highlight-rules.md).

```powershell
winssh --init-rules                  # create a commented starter file
winssh --check-rules                 # validate it and preview the colors
winssh --highlight default,journal root@10.0.0.108
```

## Install

From the [Releases](https://github.com/winssh/WinSSH/releases) page:

- **Installer** `WinSSH-setup-<version>.exe`: per-user, no admin rights. Optionally adds `winssh`
  to your PATH and adds WinSSH (and one entry per profile) to the Windows Terminal menu.
- **Portable** `WinSSH-portable-<version>.zip`: unzip and run `WinSSH.exe`. To add it to Windows
  Terminal, run `WinSSH.exe --wt-install` once (again after moving the exe).

### Windows Terminal

```powershell
winssh --wt-install      # "WinSSH" (picker) + "WinSSH: <profile>" for each profile
winssh --wt-uninstall
```

Open a new Windows Terminal window to see the entries. After `--wt-install`, the entries follow
your profiles automatically (add, rename, delete). WinSSH writes a
[JSON fragment](https://learn.microsoft.com/windows/terminal/json-fragment-extensions) and never
edits Windows Terminal's `settings.json`. Set `"wtTab": { "color": "#2d7d46" }` in a profile to
color its tab.

## Development

### Prerequisites

- **Visual Studio 2022 or newer** with the "Desktop development with C++" workload
- **CMake** 3.21 or newer on `PATH`
- **vcpkg**, with the `VCPKG_ROOT` environment variable set to its folder

### Build and test

```powershell
cmake --preset default          # first run builds dependencies with vcpkg (~15 min)
cmake --build --preset release
ctest --preset release
.\build\Release\WinSSH.exe --version
```

The build uses the `x64-windows-static` vcpkg triplet and the static MSVC runtime,
so `WinSSH.exe` runs without extra DLLs. Local builds report version `0.0.0-dev`.

### Release

Merge `develop` into `main`, then tag `main`; the release workflow builds, tests and publishes
the installer, the portable zip and `SHA256SUMS.txt`. The version comes from the tag.

```powershell
git tag v0.2.0-rc1; git push origin v0.2.0-rc1     # pre-release
git tag v0.2.0; git push origin v0.2.0             # release
```

To package locally (needs Inno Setup 6):

```powershell
cmake --preset default -DWINSSH_VERSION=0.2.0-rc1
cmake --build --preset release
.\scripts\package.ps1 -Version 0.2.0-rc1          # -> build\dist
```

### Layout

```text
src/
  main.cpp          entry point
  app/              session flow (connect, host key, auth, shell) and the console <-> channel bridge
  cli/              command-line parsing
  config/           settings files (highlight.json, profiles.json, history.json), ~/.ssh/config import
  console/          console modes, raw input thread, UTF-8 safe output, prompts
  highlight/        VT scanner, PCRE2 rule engine, highlighter
  picker/           connection picker (TUI) and the Win32 profile dialog
  ssh/              libssh2 session, known_hosts, authentication
  util/             small helpers (UTF-8, base64, paths)
  vault/            saved passwords in Windows Credential Manager
  wt/               Windows Terminal fragment (profiles in the WT menu)
  resources/        icon, version info, application manifest
tests/              Catch2 unit tests (run with CTest)
tests/e2e/          end-to-end scripts against a real SSH host
tools/conpty-run/   test driver: runs WinSSH in a pseudo console and checks its output
cmake/              Version.h template
installer/          Inno Setup script
scripts/            packaging (portable zip + installer)
spike/              throwaway M0 experiments (not part of the build)
docs/PLAN.md        plan, decisions, milestones
```

More modules are added in later milestones (see docs/PLAN.md).

### End-to-end tests

Needs a host that accepts your SSH key or agent and is already in `known_hosts`:

```powershell
.\tests\e2e\run.ps1 -Target user@host
.\tests\e2e\run.ps1 -Target user@host -Script tests\e2e\custom-rules.txt -ConfigFixture tests\e2e\rules\custom.json
```

Tests use their own settings folder (`build\e2e-config`), so your own `highlight.json` does not affect them.

## License

MIT. See [LICENSE](LICENSE).
