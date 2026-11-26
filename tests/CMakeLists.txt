find_package(Catch2 3 CONFIG REQUIRED)

add_executable(winssh_tests
    test_command_line.cpp
    test_config.cpp
    test_console.cpp
    test_escape_keys.cpp
    test_highlight.cpp
    test_rules.cpp
    test_utf8.cpp
)
target_link_libraries(winssh_tests PRIVATE winssh_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(winssh_tests)
