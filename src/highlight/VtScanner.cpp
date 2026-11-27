#include "highlight/VtScanner.h"

namespace winssh::highlight
{
namespace
{
constexpr char kEsc = '\x1b';
constexpr char kBel = '\x07';
constexpr size_t kMaxSequence = 4096; // runaway protection: flush as "Other" beyond this

// Bytes treated as text. Tab stays inside text runs; other C0 controls and DEL end them.
bool IsText(unsigned char c)
{
    return c == '\t' || (c >= 0x20 && c != 0x7f);
}
} // namespace

VtScanner::VtScanner(TextHandler onText, ControlHandler onControl)
    : onText_(std::move(onText)), onControl_(std::move(onControl))
{
}

ControlKind VtScanner::Classify(std::string_view csi)
{
    // csi = ESC [ params intermediates final
    if (csi.size() < 3)
        return ControlKind::Other;
    const char final = csi.back();
    const std::string_view params = csi.substr(2, csi.size() - 3);
    if (final == 'm' && (params.empty() || params.find_first_not_of("0123456789;:") == std::string_view::npos))
        return ControlKind::Sgr;
    if ((final == 'h' || final == 'l') && !params.empty() && params[0] == '?')
    {
        // Private modes may be a list: ?1049;25h
        std::string_view list = params.substr(1);
        while (!list.empty())
        {
            const auto semi = list.find(';');
            const std::string_view mode = list.substr(0, semi);
            if (mode == "1049" || mode == "1047" || mode == "47")
                return final == 'h' ? ControlKind::AltScreenOn : ControlKind::AltScreenOff;
            if (semi == std::string_view::npos)
                break;
            list.remove_prefix(semi + 1);
        }
    }
    return ControlKind::Other;
}

void VtScanner::EndSequence(ControlKind kind)
{
    onControl_(seq_, kind);
    seq_.clear();
    state_ = State::Ground;
}

void VtScanner::Feed(std::string_view bytes)
{
    size_t i = 0;
    const size_t n = bytes.size();
    while (i < n)
    {
        if (state_ == State::Ground)
        {
            // Longest run of text bytes.
            size_t j = i;
            while (j < n && IsText(static_cast<unsigned char>(bytes[j])))
                ++j;
            if (j > i)
            {
                onText_(bytes.substr(i, j - i));
                i = j;
                continue;
            }
            const char c = bytes[i++];
            if (c == kEsc)
            {
                seq_.assign(1, c);
                state_ = State::Esc;
            }
            else
            {
                onControl_(std::string_view(&bytes[i - 1], 1), ControlKind::Other); // \r \n \b ...
            }
            continue;
        }

        const char c = bytes[i++];
        seq_ += c;
        switch (state_)
        {
        case State::Esc:
            if (c == '[')
                state_ = State::Csi;
            else if (c == ']' || c == 'P' || c == 'X' || c == '^' || c == '_')
                state_ = State::String;
            else if (c >= 0x20 && c <= 0x2f)
                ; // intermediate byte (e.g. ESC ( B): wait for the final byte
            else
                EndSequence(ControlKind::Other); // two-byte sequence: ESC 7, ESC =, ...
            break;
        case State::Csi:
            if (c >= 0x40 && c <= 0x7e)
                EndSequence(Classify(seq_));
            else if (seq_.size() > kMaxSequence)
                EndSequence(ControlKind::Other);
            break;
        case State::String:
            if (c == kBel)
                EndSequence(ControlKind::Other);
            else if (c == kEsc)
                state_ = State::StringEsc;
            else if (seq_.size() > kMaxSequence)
                EndSequence(ControlKind::Other);
            break;
        case State::StringEsc:
            if (c == '\\')
                EndSequence(ControlKind::Other); // ST
            else
                state_ = State::String;
            break;
        case State::Ground:
            break;
        }
    }
}

} // namespace winssh::highlight
