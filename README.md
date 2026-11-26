# WinSSH

![WinSSH icon](assets/icons/WinSSH.png)

**SSH terminal for Windows, built to run as a Windows Terminal tab, with keyword highlighting you can configure.**

> Status: early development (milestone M3). Interactive SSH sessions with keyword highlighting and
> custom rules work; profiles and Windows Terminal integration come next.
> See [docs/PLAN.md](docs/PLAN.md) for goals, design and milestones.

## Usage

```text
winssh                          Open the connection picker
winssh [options] [user@]host[:port]

  -p <port>       Port (default 22)
  -l <user>       User name
  -i <keyfile>    Private key file
  --highlight <sets>  Rule sets to use, comma separated (default: default)
  --no-highlight  Turn off keyword highlighting
  --init-rules / --check-rules / --test-rules "<text>"   Manage highlight rules
  -V, --version   Show version
  -h, --help      Show this help
```

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
so `WinSSH.exe` runs without extra DLLs.

### Layout

```text
src/
  main.cpp          entry point
  app/              session flow (connect, host key, auth, shell) and the console <-> channel bridge
  cli/              command-line parsing
  config/           settings files (highlight.json)
  console/          console modes, raw input thread, UTF-8 safe output, prompts
  highlight/        VT scanner, PCRE2 rule engine, highlighter
  ssh/              libssh2 session, known_hosts, authentication
  util/             small helpers (UTF-8, base64, paths)
  resources/        icon, version info, application manifest
tests/              Catch2 unit tests (run with CTest)
tests/e2e/          end-to-end scripts against a real SSH host
tools/conpty-run/   test driver: runs WinSSH in a pseudo console and checks its output
cmake/              Version.h template
spike/              throwaway M0 experiments (not part of the build)
docs/PLAN.md        plan, decisions, milestones
```

More modules (`credentials/`, `wt/`, ...) are added in later milestones.

### End-to-end tests

Needs a host that accepts your SSH key or agent and is already in `known_hosts`:

```powershell
.\tests\e2e\run.ps1 -Target user@host
.\tests\e2e\run.ps1 -Target user@host -Script tests\e2e\custom-rules.txt -ConfigFixture tests\e2e\rules\custom.json
```

Tests use their own settings folder (`build\e2e-config`), so your own `highlight.json` does not affect them.

## License

MIT. See [LICENSE](LICENSE).
