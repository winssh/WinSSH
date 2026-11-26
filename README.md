# WinSSH

![WinSSH icon](assets/icons/WinSSH.png)

**SSH terminal for Windows, built to run as a Windows Terminal tab, with keyword highlighting you can configure.**

> Status: early development (milestone M1). Interactive SSH sessions work (key/agent, password,
> keyboard-interactive); highlighting, profiles and Windows Terminal integration come next.
> See [docs/PLAN.md](docs/PLAN.md) for goals, design and milestones.

## Usage

```text
winssh                          Open the connection picker
winssh [options] [user@]host[:port]

  -p <port>       Port (default 22)
  -l <user>       User name
  -i <keyfile>    Private key file
  -V, --version   Show version
  -h, --help      Show this help
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
  console/          console modes, raw input thread, UTF-8 safe output, prompts
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

More modules (`highlight/`, `config/`, `wt/`, ...) are added in later milestones.

### End-to-end tests

Needs a host that accepts your SSH key or agent and is already in `known_hosts`:

```powershell
.\tests\e2e\run.ps1 -Target user@host
```

## License

MIT. See [LICENSE](LICENSE).
