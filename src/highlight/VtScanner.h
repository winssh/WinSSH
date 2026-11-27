// Splits a terminal output stream into printable text and control sequences.
// Escape sequences split across reads are reassembled, so a highlighter only ever sees whole
// text runs and never matches inside an escape sequence.
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace winssh::highlight
{

enum class ControlKind
{
    Other,        // cursor movement, OSC title, C0 controls (\r, \n, \b, ...), etc.
    Sgr,          // CSI ... m  (colors / attributes)
    AltScreenOn,  // CSI ? 1049 h, ? 1047 h, ? 47 h
    AltScreenOff, // CSI ? 1049 l, ? 1047 l, ? 47 l
};

class VtScanner
{
public:
    using TextHandler = std::function<void(std::string_view text)>;
    using ControlHandler = std::function<void(std::string_view sequence, ControlKind kind)>;

    VtScanner(TextHandler onText, ControlHandler onControl);

    // Text runs are reported as soon as they end (at a control byte or at the end of `bytes`);
    // an incomplete escape sequence at the end is kept until the next Feed().
    void Feed(std::string_view bytes);

    // Forgets a half-received escape sequence (e.g. when the connection was replaced).
    void Reset()
    {
        state_ = State::Ground;
        seq_.clear();
    }

    // True while an escape sequence is incomplete.
    bool InSequence() const { return state_ != State::Ground; }

    // Classifies a complete CSI sequence (exposed for tests).
    static ControlKind Classify(std::string_view csi);

private:
    enum class State
    {
        Ground,
        Esc,       // after ESC
        Csi,       // ESC [ ...
        String,    // OSC / DCS / SOS / PM / APC body, ends with BEL or ESC \ (ST)
        StringEsc, // ESC inside a string (maybe ST)
    };

    void EndSequence(ControlKind kind);

    TextHandler onText_;
    ControlHandler onControl_;
    State state_ = State::Ground;
    std::string seq_; // bytes of the sequence being assembled
};

} // namespace winssh::highlight
