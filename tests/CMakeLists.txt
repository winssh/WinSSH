find_package(Catch2 3 CONFIG REQUIRED)

add_executable(winssh_tests
    test_command_line.cpp
    test_config.cpp
    test_console.cpp
    test_escape_keys.cpp
    test_highlight.cpp
    test_picker.cpp
    test_profiles.cpp
    test_rules.cpp
    test_utf8.cpp
    test_wt.cpp
)
target_link_libraries(winssh_tests PRIVATE winssh_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(winssh_tests)

# Release tag rules (scripts/TagVersion.psm1), plain PowerShell test script.
find_program(WINSSH_PWSH NAMES pwsh)
if(WINSSH_PWSH)
    add_test(NAME tag-version-rules
             COMMAND "${WINSSH_PWSH}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/TagVersion.Tests.ps1")
    add_test(NAME powershell-scripts-parse
             COMMAND "${WINSSH_PWSH}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Parse-Scripts.Tests.ps1")
else()
    message(STATUS "pwsh not found: tag-version-rules test skipped")
endif()
