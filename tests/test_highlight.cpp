#include "highlight/Highlighter.h"
#include "highlight/VtScanner.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace winssh::highlight;

namespace
{

// Whole-word, case-sensitive keyword matcher: enough to test the highlighter without a regex engine.
class KeywordMatcher : public Matcher
{
public:
    explicit KeywordMatcher(std::vector<std::string> words) : words_(std::move(words)) {}

    void FindAll(std::string_view text, std::vector<Match> &out) const override
    {
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t best = std::string_view::npos;
            int rule = -1;
            for (size_t r = 0; r < words_.size(); ++r)
            {
                for (size_t f = text.find(words_[r], pos); f != std::string_view::npos; f = text.find(words_[r], f + 1))
                {
                    const size_t e = f + words_[r].size();
                    const bool left = f == 0 || !IsWord(text[f - 1]);
                    const bool right = e == text.size() || !IsWord(text[e]);
                    if (left && right)
                    {
                        if (f < best)
                        {
                            best = f;
                            rule = static_cast<int>(r);
                        }
                        break;
                    }
                }
            }
            if (rule < 0)
                return;
            out.push_back({best, best + words_[static_cast<size_t>(rule)].size(), rule});
            pos = best + words_[static_cast<size_t>(rule)].size();
        }
    }

private:
    static bool IsWord(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
    std::vector<std::string> words_;
};

const std::string RED = "\x1b[31m";
const std::string GREEN = "\x1b[32m";
const std::string RST = "\x1b[0m";

Highlighter Make()
{
    return Highlighter(std::make_shared<KeywordMatcher>(std::vector<std::string>{"error", "ok"}),
                       std::vector<std::string>{RED, GREEN});
}

std::string All(Highlighter &h, std::initializer_list<std::string> chunks)
{
    std::string out;
    for (const auto &c : chunks)
        out += h.Process(c);
    return out + h.Flush();
}

struct Event
{
    std::string bytes;
    bool text;
    ControlKind kind;
};

std::vector<Event> Scan(std::initializer_list<std::string> chunks)
{
    std::vector<Event> ev;
    VtScanner s([&](std::string_view t) { ev.push_back({std::string(t), true, ControlKind::Other}); },
                [&](std::string_view q, ControlKind k) { ev.push_back({std::string(q), false, k}); });
    for (const auto &c : chunks)
        s.Feed(c);
    return ev;
}

} // namespace

TEST_CASE("VtScanner splits text and control sequences", "[vt]")
{
    auto ev = Scan({"ab\x1b[31mcd\r\n"});
    REQUIRE(ev.size() == 5);
    CHECK((ev[0].text && ev[0].bytes == "ab"));
    CHECK((!ev[1].text && ev[1].bytes == "\x1b[31m" && ev[1].kind == ControlKind::Sgr));
    CHECK((ev[2].text && ev[2].bytes == "cd"));
    CHECK((!ev[3].text && ev[3].bytes == "\r"));
    CHECK((!ev[4].text && ev[4].bytes == "\n"));
}

TEST_CASE("VtScanner reassembles a sequence split across reads", "[vt]")
{
    auto ev = Scan({"x\x1b[3", "1;1", "my"});
    REQUIRE(ev.size() == 3);
    CHECK(ev[0].bytes == "x");
    CHECK((ev[1].bytes == "\x1b[31;1m" && ev[1].kind == ControlKind::Sgr));
    CHECK(ev[2].bytes == "y");
}

TEST_CASE("VtScanner handles OSC titles ended by BEL or ST", "[vt]")
{
    auto ev = Scan({"\x1b]0;error title\x07", "\x1b]2;t\x1b\\", "z"});
    REQUIRE(ev.size() == 3);
    CHECK(ev[0].bytes == "\x1b]0;error title\x07");
    CHECK(ev[1].bytes == "\x1b]2;t\x1b\\");
    CHECK((ev[2].text && ev[2].bytes == "z"));
}

TEST_CASE("VtScanner classifies alternate screen switches", "[vt]")
{
    CHECK(VtScanner::Classify("\x1b[?1049h") == ControlKind::AltScreenOn);
    CHECK(VtScanner::Classify("\x1b[?1049l") == ControlKind::AltScreenOff);
    CHECK(VtScanner::Classify("\x1b[?25;1049h") == ControlKind::AltScreenOn);
    CHECK(VtScanner::Classify("\x1b[?25h") == ControlKind::Other);
    CHECK(VtScanner::Classify("\x1b[m") == ControlKind::Sgr);
    CHECK(VtScanner::Classify("\x1b[38;5;242m") == ControlKind::Sgr);
    CHECK(VtScanner::Classify("\x1b[2J") == ControlKind::Other);
}

TEST_CASE("Highlighter colors whole-word matches", "[highlight]")
{
    auto h = Make();
    CHECK(All(h, {"an error, then ok\r\n"}) == "an " + RED + "error" + RST + ", then " + GREEN + "ok" + RST + "\r\n");
    CHECK(All(h, {"errors okay\r\n"}) == "errors okay\r\n");
}

TEST_CASE("Highlighter never matches inside escape sequences", "[highlight]")
{
    auto h = Make();
    const std::string title = "\x1b]0;error\x07";
    CHECK(All(h, {title + "x\r\n"}) == title + "x\r\n");
}

TEST_CASE("Highlighter restores the server's colors after a match", "[highlight]")
{
    auto h = Make();
    const std::string blue = "\x1b[34m";
    const std::string bold = "\x1b[1m";
    CHECK(All(h, {blue + bold + "x error y"}) == blue + bold + "x " + RED + "error" + RST + blue + bold + " y");
    // After the server resets, nothing is replayed.
    CHECK(All(h, {"\x1b[0m" + std::string("error")}) == "\x1b[0m" + RED + "error" + RST);
}

TEST_CASE("Highlighter matches a keyword split across reads", "[highlight]")
{
    auto h = Make();
    std::string out = h.Process("line one err");
    CHECK(out.empty()); // short partial line: held whole
    CHECK(h.HasPending());
    out += h.Process("or here\r\n");
    CHECK(out == "line one " + RED + "error" + RST + " here\r\n");
    CHECK_FALSE(h.HasPending());
}

TEST_CASE("Highlighter holds only the last word of a long partial line", "[highlight]")
{
    auto h = Make();
    const std::string head(Highlighter::kMaxHoldBack, 'x');
    std::string out = h.Process(head + " err");
    CHECK(out == head + " ");
    out += h.Process("or");
    out += h.Flush();
    CHECK(out == head + " " + RED + "error" + RST);
}

TEST_CASE("Highlighter flush emits a held partial line", "[highlight]")
{
    auto h = Make();
    CHECK(h.Process("$ ok").empty());
    CHECK(h.Flush() == "$ " + GREEN + "ok" + RST);
}

TEST_CASE("Highlighter does not hold back long words or text before an open sequence", "[highlight]")
{
    auto h = Make();
    const std::string longWord(Highlighter::kMaxHoldBack + 1, 'x');
    CHECK(h.Process(longWord) == longWord);
    CHECK(h.Process("ok\x1b[3") == GREEN + "ok" + RST);
    CHECK(h.Process("1m") == "\x1b[31m");
}

TEST_CASE("Highlighter pauses in the alternate screen", "[highlight]")
{
    auto h = Make();
    CHECK(All(h, {"\x1b[?1049h", "error\r\n"}) == "\x1b[?1049herror\r\n");
    CHECK(h.InAltScreen());
    CHECK(All(h, {"\x1b[?1049l", "error"}) == "\x1b[?1049l" + RED + "error" + RST);
}

TEST_CASE("Highlighter passes text through when disabled", "[highlight]")
{
    auto h = Make();
    h.SetEnabled(false);
    CHECK(h.Process("error ok") == "error ok"); // no hold-back either
}

TEST_CASE("Highlighter keeps UTF-8 intact around matches", "[highlight]")
{
    auto h = Make();
    CHECK(All(h, {"thi\xE1\xBA\xBFt b\xE1\xBB\x8B error \xE2\x9C\x93"}) ==
          "thi\xE1\xBA\xBFt b\xE1\xBB\x8B " + RED + "error" + RST + " \xE2\x9C\x93");
}
