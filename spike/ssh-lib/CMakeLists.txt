cmake_minimum_required(VERSION 3.15)
project(winssh_spike_ssh_lib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

find_package(libssh CONFIG REQUIRED)
find_package(libssh2 CONFIG REQUIRED)

add_executable(spike_libssh spike_libssh.cpp)
target_link_libraries(spike_libssh PRIVATE ssh ws2_32)

add_executable(spike_libssh2 spike_libssh2.cpp)
target_link_libraries(spike_libssh2 PRIVATE libssh2::libssh2 ws2_32)
