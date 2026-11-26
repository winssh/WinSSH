// Writes remote output to the console, never splitting a UTF-8 character across writes.
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace winssh::console
{

class OutputWriter
{
public:
    using Sink = std::function<void(std::string_view)>;

    // Default sink writes bytes to stdout (console code page is UTF-8).
    OutputWriter();
    explicit OutputWriter(Sink sink);

    void Write(std::string_view bytes);
    // Writes any held-back bytes (call at the end of the session).
    void Flush();

private:
    Sink sink_;
    std::string pending_;
};

} // namespace winssh::console
