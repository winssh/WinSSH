cmake_minimum_required(VERSION 3.21)
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(winssh_spike_regex LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(PCRE2 CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)

add_executable(regex_bench bench.cpp)
target_compile_definitions(regex_bench PRIVATE PCRE2_STATIC NOMINMAX)
target_compile_options(regex_bench PRIVATE /utf-8 /O2)
target_link_libraries(regex_bench PRIVATE PCRE2::8BIT re2::re2)
