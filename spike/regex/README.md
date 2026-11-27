# M2 spike: regex engine for highlighting

Throwaway benchmark for decision D5 in `docs/PLAN.md` (section 4.4). Not part of the app.

Compares `std::regex`, PCRE2 (JIT) and RE2 on WinSerial's default rules combined into one pattern:
speed, match counts (must be equal), and a catastrophic-backtracking rule.

## Build and run

```powershell
cd spike\regex
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
.\build\Release\regex_bench.exe [captured-output.log ...]
```

The first configure builds RE2 and abseil with vcpkg (slow). Extra arguments are log files
appended to the synthetic corpus, e.g. `build\e2e-*.log` from the end-to-end tests.
