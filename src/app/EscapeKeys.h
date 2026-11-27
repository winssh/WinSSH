// Escape-key command mode: Ctrl+] then a key runs a local command instead of sending input.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace winssh::app
{

enum class Command
{
    Help,
    ToggleHighlight,
    Info,
    Reconnect,
    Exit,
    Unknown,
};

struct EscapeResult
{
    std::string toRemote;          // keys to send to the server
    std::vector<Command> commands; // local commands, in order
};

class EscapeKeys
{
public:
    static constexpr char kDefaultEscape = '\x1d'; // Ctrl+]

    explicit EscapeKeys(char escape = kDefaultEscape) : escape_(escape) {}

    // Feeds typed keys. After the escape key, the next key is a command:
    //   h or ?  help        f  toggle highlighting     i  connection info
    //   r       reconnect   x  exit        escape key again: send the escape key itself
    // Letters also work with Ctrl held (Ctrl+F = f), except Ctrl+H/Ctrl+I which are Backspace/Tab.
    EscapeResult Feed(std::string_view keys);

    bool Armed() const { return armed_; }

    static std::string HelpText();

private:
    char escape_;
    bool armed_ = false;
};

} // namespace winssh::app
