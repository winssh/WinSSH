# Core library: everything except main(), so unit tests can link it.
add_library(winssh_core STATIC
    cli/CommandLine.cpp
    cli/CommandLine.h
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
target_link_libraries(winssh_core PUBLIC Boost::boost libssh2::libssh2)

# Application
add_executable(WinSSH
    main.cpp
    resources/WinSSH.rc
    resources/WinSSH.manifest
    resources/resource.h
)
target_link_libraries(WinSSH PRIVATE winssh_core)
set_target_properties(WinSSH PROPERTIES OUTPUT_NAME "WinSSH")
