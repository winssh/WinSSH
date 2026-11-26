// Console setup and simple line prompts.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace winssh::console
{

struct Size
{
    int cols = 80;
    int rows = 24;
};

// Saves the console input/output modes and code pages, restores them in the destructor.
// Create one at the start of main so every exit path restores the user's console.
class ConsoleModeGuard
{
public:
    ConsoleModeGuard();
    ~ConsoleModeGuard();
    ConsoleModeGuard(const ConsoleModeGuard &) = delete;
    ConsoleModeGuard &operator=(const ConsoleModeGuard &) = delete;

    // UTF-8 code pages + VT processing on output.
    void EnableVtOutput();
    // Raw VT input for the interactive session: no line editing, no echo, no Ctrl+C processing;
    // keys arrive as VT sequences and window size changes as events.
    void EnableRawInput();
    // Back to the saved input mode (used before a prompt during the session, if ever needed).
    void RestoreInput();

private:
    void *in_ = nullptr;
    void *out_ = nullptr;
    unsigned long inMode_ = 0;
    unsigned long outMode_ = 0;
    unsigned int inCp_ = 0;
    unsigned int outCp_ = 0;
    bool haveIn_ = false;
    bool haveOut_ = false;
};

// True when stdin is an interactive console (not a pipe or file).
bool StdinIsConsole();

Size GetSize();

void SetTitle(std::string_view utf8);

// Writes UTF-8 text to stderr (status and error messages).
void PrintError(std::string_view utf8);

// Reads one line from the console in cooked mode. With echo == false the typed text is hidden
// (passwords). Returns std::nullopt on Ctrl+C, Ctrl+Z/EOF or when stdin is not a console.
std::optional<std::string> ReadLine(std::string_view prompt, bool echo);

// Handles Ctrl+C / Ctrl+Break signals instead of letting Windows kill the process (which would
// skip ConsoleModeGuard and could leave the console with echo off). The handler runs on a system
// thread. Signals only happen in cooked input mode (connecting, prompts); in raw mode Ctrl+C is a
// normal key (0x03) sent to the server. A prompt interrupted by Ctrl+C returns std::nullopt.
void SetInterruptHandler(std::function<void()> handler);

} // namespace winssh::console
