#include "highlight/Highlighter.h"

namespace winssh::highlight
{
namespace
{
constexpr std::string_view kReset = "\x1b[0m";
constexpr size_t kMaxActiveSgr = 1024;

// True when an SGR sequence starts by resetting all attributes: ESC[m, ESC[0m, ESC[0;31m ...
bool ResetsAll(std::string_view sgr)
{
    const std::string_view params = sgr.substr(2, sgr.size() - 3);
    return params.empty() || params == "0" || params.rfind("0;", 0) == 0;
}
} // namespace

Highlighter::Highlighter(std::shared_ptr<const Matcher> matcher, std::vector<std::string> styles)
    : matcher_(std::move(matcher)), styles_(std::move(styles)),
      scanner_([this](std::string_view t) { OnText(t); },
               [this](std::string_view s, ControlKind k) { OnControl(s, k); })
{
}

std::string Highlighter::Process(std::string_view bytes)
{
    scanner_.Feed(bytes);

    // `pending_` now holds text after the last control sequence (usually the start of a line).
    // If an escape sequence is open, that text is complete (the ESC ended it). Otherwise hold it
    // back when short, so multi-word keywords ("ip address") still match when the line goes on
    // in the next read; for a long run hold back only its last word.
    if (!pending_.empty())
    {
        size_t keep = 0;
        if (Active() && !scanner_.InSequence())
        {
            if (pending_.size() <= kMaxHoldBack)
            {
                keep = pending_.size();
            }
            else
            {
                const auto ws = pending_.find_last_of(" \t");
                const size_t tail = ws == std::string::npos ? pending_.size() : pending_.size() - ws - 1;
                if (tail <= kMaxHoldBack)
                    keep = tail;
            }
        }
        Emit(std::string_view(pending_).substr(0, pending_.size() - keep));
        pending_.erase(0, pending_.size() - keep);
    }
    std::string result;
    result.swap(out_);
    return result;
}

std::string Highlighter::Flush()
{
    if (!pending_.empty())
    {
        Emit(pending_);
        pending_.clear();
    }
    std::string result;
    result.swap(out_);
    return result;
}

void Highlighter::OnText(std::string_view text)
{
    pending_.append(text);
}

void Highlighter::OnControl(std::string_view seq, ControlKind kind)
{
    if (!pending_.empty())
    {
        Emit(pending_);
        pending_.clear();
    }
    out_.append(seq);

    switch (kind)
    {
    case ControlKind::Sgr:
        if (ResetsAll(seq))
            activeSgr_.assign(seq);
        else if (activeSgr_.size() + seq.size() <= kMaxActiveSgr)
            activeSgr_.append(seq);
        if (activeSgr_ == "\x1b[m" || activeSgr_ == kReset)
            activeSgr_.clear();
        break;
    case ControlKind::AltScreenOn:
        altScreen_ = true;
        break;
    case ControlKind::AltScreenOff:
        altScreen_ = false;
        break;
    case ControlKind::Other:
        break;
    }
}

void Highlighter::Emit(std::string_view text)
{
    if (!Active())
    {
        out_.append(text);
        return;
    }
    matches_.clear();
    matcher_->FindAll(text, matches_);
    size_t pos = 0;
    for (const Match &m : matches_)
    {
        if (m.begin < pos || m.end <= m.begin || m.end > text.size() || m.rule < 0 ||
            static_cast<size_t>(m.rule) >= styles_.size())
            continue; // overlapping or invalid
        out_.append(text.substr(pos, m.begin - pos));
        out_.append(styles_[static_cast<size_t>(m.rule)]);
        out_.append(text.substr(m.begin, m.end - m.begin));
        out_.append(kReset);
        out_.append(activeSgr_); // back to the server's colors
        pos = m.end;
    }
    out_.append(text.substr(pos));
}

} // namespace winssh::highlight
