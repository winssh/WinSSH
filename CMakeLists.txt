cmake_minimum_required(VERSION 3.21)

# Static vcpkg triplet + static MSVC runtime: WinSSH ships as a single .exe with no DLLs.
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "vcpkg triplet")
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(WINSSH_BUILD_TESTS "Build unit tests" ON)
if(WINSSH_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(WinSSH VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "WinSSH only builds on Windows")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All executables in build/<Config>/ (e.g. build/Release/WinSSH.exe)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

# Version header with the short git commit hash
find_package(Git QUIET)
set(PROJECT_VERSION_COMMIT "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_rc)
    if(_git_rc EQUAL 0 AND _commit)
        set(PROJECT_VERSION_COMMIT "${_commit}")
    endif()
endif()
configure_file(cmake/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h" @ONLY)

find_package(Boost CONFIG REQUIRED)
find_package(libssh2 CONFIG REQUIRED)

add_subdirectory(src)

if(WINSSH_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
