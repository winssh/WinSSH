# Test helper: drive a console program inside a pseudo console (see conpty-run/main.cpp).
add_executable(conpty-run conpty-run/main.cpp)
target_compile_definitions(conpty-run PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(conpty-run PRIVATE /W4 /permissive- /utf-8)
