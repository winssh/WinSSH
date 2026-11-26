// Inserts colors around rule matches in terminal output without breaking the server's own
// escape sequences or colors.
#pragma once

#include "highlight/VtScanner.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace winssh::highlight
{

struct Match
{
    size_t begin = 0;
    size_t end = 0;
    int rule = 0;
};

// Finds non-overlapping rule matches in a text run (no escape sequences, no line breaks).
class Matcher
{
public:
    virtual ~Matcher() = default;
    virtual void FindAll(std::string_view text, std::vector<Match> &out) const = 0;
};

class Highlighter
{
public:
    // styles[i] is the SGR sequence (e.g. "\x1b[1;31m") for rule i.
    Highlighter(std::shared_ptr<const Matcher> matcher, std::vector<std::string> styles);

    // Processes a chunk of remote output and returns the bytes to write now. A short trailing
    // word may be held back so a keyword split across two reads still matches; call Flush() when
    // no more data arrives soon (the session does this after ~10 ms).
    std::string Process(std::string_view bytes);
    std::string Flush();
    bool HasPending() const { return !pending_.empty(); }

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool Enabled() const { return enabled_; }
    // Full-screen apps (vim, htop, less) use the alternate screen; highlighting pauses there.
    bool InAltScreen() const { return altScreen_; }

    static constexpr size_t kMaxHoldBack = 64;

private:
    void OnText(std::string_view text);
    void OnControl(std::string_view seq, ControlKind kind);
    void Emit(std::string_view text);
    bool Active() const { return enabled_ && !altScreen_ && matcher_ != nullptr; }

    std::shared_ptr<const Matcher> matcher_;
    std::vector<std::string> styles_;
    VtScanner scanner_;
    std::string out_;
    std::string pending_;   // text not yet emitted (current run)
    std::string activeSgr_; // server's SGR sequences since its last reset, replayed after a match
    std::vector<Match> matches_;
    bool enabled_ = true;
    bool altScreen_ = false;
};

} // namespace winssh::highlight
