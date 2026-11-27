// Background thread that reads console input records in raw VT mode.
#pragma once

#include "console/Console.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace winssh::console
{

class InputReader
{
public:
    using KeysHandler = std::function<void(std::string utf8)>;
    using ResizeHandler = std::function<void(Size)>;

    InputReader() = default;
    ~InputReader();
    InputReader(const InputReader &) = delete;
    InputReader &operator=(const InputReader &) = delete;

    // Handlers are called on the reader thread; post to your own thread from there.
    void Start(KeysHandler onKeys, ResizeHandler onResize);
    void Stop();

private:
    void Run();

    KeysHandler onKeys_;
    ResizeHandler onResize_;
    std::thread thread_;
    void *stopEvent_ = nullptr;
};

// Converts console key-down characters to UTF-8, joining surrogate pairs across calls.
class Utf16Joiner
{
public:
    // Appends the UTF-8 form of `ch` to `out` (nothing while waiting for a low surrogate).
    void Push(wchar_t ch, std::string &out);

private:
    wchar_t high_ = 0;
};

} // namespace winssh::console
