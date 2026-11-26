#include "app/EscapeKeys.h"

namespace winssh::app
{

EscapeResult EscapeKeys::Feed(std::string_view keys)
{
    EscapeResult r;
    for (char c : keys)
    {
        if (!armed_)
        {
            if (c == escape_)
                armed_ = true;
            else
                r.toRemote += c;
            continue;
        }

        armed_ = false;
        if (c == escape_)
        {
            r.toRemote += c; // escape key twice: send it
            continue;
        }
        // Ctrl+letter arrives as 0x01..0x1a; map to the letter (not Ctrl+H/Ctrl+I: Backspace/Tab).
        char k = c;
        if (k >= 0x01 && k <= 0x1a && k != 0x08 && k != 0x09)
            k = static_cast<char>('a' + k - 1);
        if (k >= 'A' && k <= 'Z')
            k = static_cast<char>(k - 'A' + 'a');
        switch (k)
        {
        case 'h':
        case '?': r.commands.push_back(Command::Help); break;
        case 'f': r.commands.push_back(Command::ToggleHighlight); break;
        case 'i': r.commands.push_back(Command::Info); break;
        case 'x': r.commands.push_back(Command::Exit); break;
        default: r.commands.push_back(Command::Unknown); break;
        }
    }
    return r;
}

std::string EscapeKeys::HelpText()
{
    return "WinSSH commands: press Ctrl+] then\r\n"
           "  h  this help\r\n"
           "  f  toggle keyword highlighting\r\n"
           "  i  connection info\r\n"
           "  x  disconnect and exit\r\n"
           "  Ctrl+]  send Ctrl+] to the server\r\n";
}

} // namespace winssh::app
