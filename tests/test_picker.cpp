#include "picker/PickerModel.h"
#include "picker/PickerView.h"
#include "picker/ProfileForm.h"

#include <catch2/catch_test_macros.hpp>

using namespace winssh;
using namespace winssh::picker;

namespace
{
const std::string UP = "\x1b[A", DOWN = "\x1b[B", ENTER = "\r", ESC = "\x1b", DEL = "\x1b[3~";

std::vector<config::Profile> Profiles()
{
    std::vector<config::Profile> ps(2);
    ps[0].id = "cb2-lab";
    ps[0].name = "CB2 lab unit";
    ps[0].host = "10.0.0.108";
    ps[0].user = "root";
    ps[0].highlight = {"default", "journal"};
    ps[1].id = "fwserver";
    ps[1].name = "fwserver";
    ps[1].host = "10.0.0.111";
    ps[1].user = "fc";
    ps[1].port = 2222;
    return ps;
}

std::vector<config::RecentEntry> Recent()
{
    return {{"admin", "192.168.1.1", 22, "", "2026-11-26T10:00:00Z"},
            {"root", "10.0.0.108", 22, "cb2-lab", "2026-11-26T09:00:00Z"}};
}

PickerModel Make()
{
    PickerModel m;
    m.SetData(Profiles(), Recent());
    return m;
}
} // namespace

TEST_CASE("SplitKeys separates characters and escape sequences", "[picker]")
{
    auto k = SplitKeys("a\x1b[A\xE1\xBA\xBF\x1b[3~\x1bOB\x1b");
    REQUIRE(k.size() == 6);
    CHECK(k[0] == "a");
    CHECK(k[1] == "\x1b[A");
    CHECK(k[2] == "\xE1\xBA\xBF");
    CHECK(k[3] == "\x1b[3~");
    CHECK(k[4] == "\x1bOB");
    CHECK(k[5] == "\x1b"); // lone ESC at the end

    // Esc then Delete in one read
    auto k2 = SplitKeys("\x1b\x1b[3~");
    REQUIRE(k2.size() == 2);
    CHECK(k2[0] == "\x1b");
    CHECK(k2[1] == "\x1b[3~");
}

TEST_CASE("Picker lists profiles then recent, first item selected", "[picker]")
{
    auto m = Make();
    REQUIRE(m.Visible().size() == 4);
    CHECK(m.Visible()[0]->kind == Item::Kind::Profile);
    CHECK(m.Visible()[0]->title == "CB2 lab unit");
    CHECK(m.Visible()[0]->detail == "root@10.0.0.108");
    CHECK(m.Visible()[0]->tags == "[default,journal]");
    CHECK(m.Visible()[1]->detail == "fc@10.0.0.111:2222");
    CHECK(m.Visible()[2]->kind == Item::Kind::Recent);
    CHECK(m.Visible()[2]->title == "admin@192.168.1.1");
    CHECK(m.Visible()[3]->title == "cb2-lab"); // recent made with a profile shows the id
    CHECK(m.Selected() == 0);
}

TEST_CASE("Picker navigation is clamped", "[picker]")
{
    auto m = Make();
    m.Feed(UP);
    CHECK(m.Selected() == 0);
    m.Feed(DOWN + DOWN + DOWN + DOWN + DOWN);
    CHECK(m.Selected() == 3);
    m.Feed("\x1b[H"); // Home
    CHECK(m.Selected() == 0);
    m.Feed("\x1b[F"); // End
    CHECK(m.Selected() == 3);
}

TEST_CASE("Picker filter matches words in any field", "[picker]")
{
    auto m = Make();
    m.Feed("journal");
    REQUIRE(m.Visible().size() == 1);
    CHECK(m.Visible()[0]->profileId == "cb2-lab");

    m.Feed("\x15"); // Ctrl+U clears
    m.Feed("FC 2222");
    REQUIRE(m.Visible().size() == 1);
    CHECK(m.Visible()[0]->profileId == "fwserver");

    m.Feed("\x7f\x7f\x7f\x7f\x7f"); // Backspace x5 -> "FC"
    CHECK(m.Filter() == "FC");
    m.Feed("zzz");
    CHECK(m.Visible().empty());
    CHECK(m.Selected() == -1);
}

TEST_CASE("Picker Enter connects to the selection", "[picker]")
{
    auto m = Make();
    m.Feed(DOWN);
    auto a = m.Feed(ENTER);
    REQUIRE(a.kind == ActionKind::Connect);
    CHECK(a.item->profileId == "fwserver");
}

TEST_CASE("Picker quick connect from user@host", "[picker]")
{
    auto m = Make();
    m.Feed("pi@10.0.0.50:2200");
    CHECK(m.FilterIsQuickTarget());
    auto a = m.Feed(ENTER);
    REQUIRE(a.kind == ActionKind::QuickConnect);
    CHECK(a.text == "pi@10.0.0.50:2200");

    // With '@', quick connect wins even when it also matches an item.
    auto m2 = Make();
    m2.Feed("root@10.0.0.108");
    CHECK(m2.Feed(ENTER).kind == ActionKind::QuickConnect);

    // A plain word is a filter, not a host.
    auto m3 = Make();
    m3.Feed("cb2");
    CHECK_FALSE(m3.FilterIsQuickTarget());
    CHECK(m3.Feed(ENTER).kind == ActionKind::Connect);
}

TEST_CASE("Picker Esc clears the filter first, then quits", "[picker]")
{
    auto m = Make();
    m.Feed("cb2");
    CHECK(m.Feed(ESC).kind == ActionKind::None);
    CHECK(m.Filter().empty());
    CHECK(m.Feed(ESC).kind == ActionKind::Quit);
    auto m2 = Make();
    CHECK(m2.Feed("\x03").kind == ActionKind::Quit); // Ctrl+C
}

TEST_CASE("Picker letters always go to the filter", "[picker]")
{
    auto m = Make();
    CHECK(m.Feed("nesd").kind == ActionKind::None);
    CHECK(m.Filter() == "nesd");
}

TEST_CASE("Picker profile commands", "[picker]")
{
    auto m = Make();
    CHECK(m.Feed("\x0e").kind == ActionKind::NewProfile); // Ctrl+N
    auto edit = m.Feed("\x05");                            // Ctrl+E on a profile
    REQUIRE(edit.kind == ActionKind::EditProfile);
    CHECK(edit.item->profileId == "cb2-lab");
    CHECK(m.Feed("\x13").kind == ActionKind::None); // Ctrl+S on a profile: only for recent
    CHECK_FALSE(m.Status().empty());

    m.Feed(DOWN + DOWN); // recent admin@192.168.1.1
    auto save = m.Feed("\x13");
    REQUIRE(save.kind == ActionKind::SaveRecent);
    CHECK(save.item->recent.host == "192.168.1.1");
    CHECK(m.Feed("\x05").kind == ActionKind::None); // edit needs a profile
}

TEST_CASE("Picker delete asks for confirmation", "[picker]")
{
    auto m = Make();
    CHECK(m.Feed(DEL).kind == ActionKind::None);
    CHECK(m.ConfirmingDelete());
    CHECK(m.Status().find("cb2-lab") != std::string::npos);
    CHECK(m.Feed("n").kind == ActionKind::None); // anything but y cancels
    CHECK_FALSE(m.ConfirmingDelete());
    CHECK(m.Filter().empty()); // the answer is not typed into the filter

    m.Feed(DEL);
    auto a = m.Feed("y");
    REQUIRE(a.kind == ActionKind::DeleteProfile);
    CHECK(a.item->profileId == "cb2-lab");
}

TEST_CASE("Picker keeps the selection across a data refresh", "[picker]")
{
    auto m = Make();
    m.Feed(DOWN); // fwserver
    m.SetData(Profiles(), Recent());
    REQUIRE(m.SelectedItem());
    CHECK(m.SelectedItem()->profileId == "fwserver");
}

TEST_CASE("PickerView draws header, sections, selection and footer", "[picker]")
{
    auto m = Make();
    PickerView v;
    const std::string frame = v.Render(m, 100, 20);
    CHECK(frame.rfind("\x1b[H", 0) == 0);
    CHECK(frame.find("select a connection") != std::string::npos);
    CHECK(frame.find("PROFILES") != std::string::npos);
    CHECK(frame.find("RECENT") != std::string::npos);
    CHECK(frame.find("\x1b[7m  > CB2 lab unit") != std::string::npos); // selected, reverse video
    CHECK(frame.find("Ctrl+N new") != std::string::npos);
}

TEST_CASE("PickerView scrolls to keep the selection visible", "[picker]")
{
    std::vector<config::Profile> many;
    for (int i = 0; i < 40; ++i)
    {
        config::Profile p;
        p.id = "host" + std::to_string(i);
        p.name = p.id;
        p.host = "10.0.0." + std::to_string(i);
        many.push_back(p);
    }
    PickerModel m;
    m.SetData(many, {});
    PickerView v;
    m.Feed("\x1b[F"); // End
    const std::string frame = v.Render(m, 80, 12);
    CHECK(frame.find("> host39") != std::string::npos);
    CHECK(frame.find("host0 ") == std::string::npos);
}

TEST_CASE("PickerView shows the quick-connect hint and the empty state", "[picker]")
{
    PickerModel m;
    m.SetData({}, {});
    PickerView v;
    CHECK(v.Render(m, 100, 15).find("No profiles or recent connections yet") != std::string::npos);
    m.Feed("root@10.0.0.108");
    CHECK(v.Render(m, 100, 15).find("Enter: connect to root@10.0.0.108") != std::string::npos);
}

TEST_CASE("FitWidth cuts by characters and adds an ellipsis", "[picker]")
{
    CHECK(FitWidth("abc", 5) == "abc");
    CHECK(FitWidth("abcdef", 4) == "abc\xE2\x80\xA6");
    CHECK(FitWidth("thi\xE1\xBA\xBFt b\xE1\xBB\x8B", 5) == "thi\xE1\xBA\xBF\xE2\x80\xA6"); // 4 chars + "…" = 5 columns
}

TEST_CASE("ProfileForm round trip and validation", "[picker]")
{
    auto ps = Profiles();
    auto f = FormFromProfile(ps[0], true);
    CHECK(f.originalId == "cb2-lab");
    CHECK(f.highlight == "default,journal");
    CHECK(f.hasSavedPassword);
    CHECK_FALSE(ValidateForm(f, ps, true)); // editing itself is fine

    auto p = ProfileFromForm(f, &ps[0]);
    CHECK(p.id == "cb2-lab");
    CHECK(p.highlight == std::vector<std::string>{"default", "journal"});

    // Rename onto another profile's id (case-insensitive) is refused.
    f.id = "FWSERVER";
    auto err = ValidateForm(f, ps, true);
    REQUIRE(err);
    CHECK(err->field == kFieldId);

    ProfileForm n;
    n.id = "new";
    n.host = "";
    CHECK(ValidateForm(n, ps, true)->field == kFieldHost);
    n.host = "root@x";
    CHECK(ValidateForm(n, ps, true)->field == kFieldHost);
    n.host = "x";
    n.port = "70000";
    CHECK(ValidateForm(n, ps, true)->field == kFieldPort);
    n.port = "22";
    n.keepAlive = "-5";
    CHECK(ValidateForm(n, ps, true)->field == kFieldKeepAlive);
    n.keepAlive = "";
    n.id = "bad id";
    CHECK(ValidateForm(n, ps, true)->field == kFieldId);
    CHECK_FALSE(ValidateForm(n, ps, false)); // Connect without saving: id not needed
}

TEST_CASE("SuggestId makes a unique, valid id from a host", "[picker]")
{
    auto ps = Profiles();
    CHECK(SuggestId("10.0.0.5", ps) == "10.0.0.5");
    CHECK(SuggestId("my host", ps) == "my-host");
    CHECK(SuggestId("fwserver", ps) == "fwserver-2");
    CHECK(config::IsValidProfileId(SuggestId("--odd name!", ps)));
}

TEST_CASE("Picker returns keys typed after the action", "[picker]")
{
    auto m = Make();
    auto a = m.Feed("\recho hi\r");
    REQUIRE(a.kind == ActionKind::Connect);
    CHECK(a.rest == "echo hi\r");
}
