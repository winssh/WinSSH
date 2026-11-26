# M0 spike: libssh vs libssh2

Throwaway code for decision D2 in `docs/PLAN.md` (section 3.2). Not part of the app.

Two console programs run the same checks against one SSH target:
connect, negotiated algorithms, host-key fingerprint, `known_hosts` match, auth methods,
agent auth, key-file auth, PTY + resize (`stty size` before/after `window-change`),
UTF-8 passthrough, remote exit code, legacy algorithm support.

Auth is key/agent only; the spike never asks for or stores a password and never reads
the private key itself (it only passes the path to the library).

## Build

```powershell
cd spike\ssh-lib
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
```

The first configure builds OpenSSL, libssh and libssh2 with vcpkg (~12 min).

## Run

```powershell
.\build\Release\spike_libssh.exe  user@host [port] [keyfile]
.\build\Release\spike_libssh2.exe user@host [port] [keyfile]
```

`keyfile` defaults to `%USERPROFILE%\.ssh\id_ed25519`. Set `SPIKE_DEBUG=1` for extra read diagnostics (libssh).
