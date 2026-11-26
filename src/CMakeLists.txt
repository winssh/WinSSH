# Core library: everything except main(), so unit tests can link it.
add_library(winssh_core STATIC
    app/Bridge.cpp
    app/Bridge.h
    app/Connect.cpp
    app/Connect.h
    app/EscapeKeys.cpp
    app/EscapeKeys.h
    cli/CommandLine.cpp
    cli/CommandLine.h
    console/Console.cpp
    console/Console.h
    console/InputReader.cpp
    console/InputReader.h
    console/OutputWriter.cpp
    console/OutputWriter.h
    highlight/Highlighter.cpp
    highlight/Highlighter.h
    highlight/Rules.cpp
    highlight/Rules.h
    highlight/VtScanner.cpp
    highlight/VtScanner.h
    ssh/Auth.cpp
    ssh/Auth.h
    ssh/KnownHosts.cpp
    ssh/KnownHosts.h
    ssh/Session.cpp
    ssh/Session.h
    util/Base64.cpp
    util/Base64.h
    util/Paths.cpp
    util/Paths.h
    util/Utf8.cpp
    util/Utf8.h
)
target_include_directories(winssh_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}/generated"
)
target_compile_definitions(winssh_core PUBLIC
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00   # Windows 10
)
target_compile_options(winssh_core PUBLIC /W4 /permissive- /utf-8)
target_link_libraries(winssh_core PUBLIC Boost::boost libssh2::libssh2 PCRE2::8BIT)
target_compile_definitions(winssh_core PRIVATE PCRE2_STATIC)

# Application
add_executable(WinSSH
    main.cpp
    resources/WinSSH.rc
    resources/WinSSH.manifest
    resources/resource.h
)
target_link_libraries(WinSSH PRIVATE winssh_core)
set_target_properties(WinSSH PROPERTIES OUTPUT_NAME "WinSSH")
