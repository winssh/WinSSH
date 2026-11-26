# WinSSH

![WinSSH icon](assets/icons/WinSSH.png)

**SSH terminal for Windows, built to run as a Windows Terminal tab, with keyword highlighting you can configure.**

> Status: early development (milestone M0, project skeleton). It cannot connect yet.
> See [docs/PLAN.md](docs/PLAN.md) for goals, design and milestones.

## Usage (planned)

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

- **Visual Studio 2022** with the "Desktop development with C++" workload
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
  main.cpp        entry point
  cli/            command-line parsing
  util/           small helpers (UTF-8 conversion)
  resources/      icon, version info, application manifest
tests/            Catch2 unit tests (run with CTest)
cmake/            Version.h template
spike/            throwaway M0 experiments (not part of the build)
docs/PLAN.md      plan, decisions, milestones
```

More modules (`console/`, `ssh/`, `highlight/`, `config/`, ...) are added in later milestones.

## License

MIT. See [LICENSE](LICENSE).
