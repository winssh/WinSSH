#include "picker/PickerModel.h"

#include "cli/CommandLine.h"

#include <algorithm>
#include <cctype>

namespace winssh::picker
{
namespace
{

std::string Address(const std::string &user, const std::string &host, int port)
{
    std::string a = user.empty() ? host : user + "@" + host;
    if (port != 22)
        a += ":" + std::to_string(port);
    return a;
}

std::string Lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Removes the last UTF-8 character.
void PopChar(std::string &s)
{
    while (!s.empty())
    {
        const unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80)
            break; // removed the lead byte
    }
}

constexpr char kEsc = '\x1b';

} // namespace

std::vector<std::string> SplitKeys(std::string_view keys)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < keys.size())
    {
        const unsigned char c = static_cast<unsigned char>(keys[i]);
        if (c == static_cast<unsigned char>(kEsc))
        {
            if (i + 1 >= keys.size())
            {
                out.emplace_back(1, kEsc); // lone ESC = Esc key
                ++i;
                continue;
            }
            const char next = keys[i + 1];
            if (next == kEsc) // Esc pressed, then another key that starts with ESC
            {
                out.emplace_back(1, kEsc);
                ++i;
                continue;
            }
            if (next == '[') // CSI: ESC [ params final
            {
                size_t j = i + 2;
                while (j < keys.size() && !(keys[j] >= 0x40 && keys[j] <= 0x7e))
                    ++j;
                const size_t end = std::min(j + 1, keys.size());
                out.emplace_back(keys.substr(i, end - i));
                i = end;
                continue;
            }
            if (next == 'O' && i + 2 < keys.size()) // SS3: ESC O A (arrows in application mode)
            {
                out.emplace_back(keys.substr(i, 3));
                i += 3;
                continue;
            }
            out.emplace_back(keys.substr(i, 2)); // Alt+key: ignored by the picker
            i += 2;
            continue;
        }
        size_t len = 1;
        if (c >= 0xF0)
            len = 4;
        else if (c >= 0xE0)
            len = 3;
        else if (c >= 0xC0)
            len = 2;
        len = std::min(len, keys.size() - i);
        out.emplace_back(keys.substr(i, len));
        i += len;
    }
    return out;
}

bool Matches(const Item &item, std::string_view filter)
{
    const std::string hay = Lower(item.title + " " + item.detail + " " + item.profileId + " " + item.tags);
    const std::string f = Lower(filter);
    size_t pos = 0;
    while (pos < f.size())
    {
        while (pos < f.size() && f[pos] == ' ')
            ++pos;
        size_t end = f.find(' ', pos);
        if (end == std::string::npos)
            end = f.size();
        if (end > pos && hay.find(f.substr(pos, end - pos)) == std::string::npos)
            return false;
        pos = end;
    }
    return true;
}

void PickerModel::SetData(const std::vector<config::Profile> &profiles, const std::vector<config::RecentEntry> &recent)
{
    std::string keepId;
    if (const Item *sel = SelectedItem())
        keepId = sel->profileId + "|" + sel->title;

    items_.clear();
    for (const auto &p : profiles)
    {
        Item it;
        it.kind = Item::Kind::Profile;
        it.title = p.name.empty() ? p.id : p.name;
        it.detail = Address(p.user, p.host, p.port);
        it.profileId = p.id;
        if (!p.highlight.empty())
        {
            it.tags = "[";
            for (size_t i = 0; i < p.highlight.size(); ++i)
                it.tags += (i ? "," : "") + p.highlight[i];
            it.tags += "]";
        }
        items_.push_back(std::move(it));
    }
    for (const auto &r : recent)
    {
        Item it;
        it.kind = Item::Kind::Recent;
        it.recent = r;
        it.profileId = r.profileId;
        it.title = r.profileId.empty() ? Address(r.user, r.host, r.port) : r.profileId;
        it.detail = r.lastConnected;
        items_.push_back(std::move(it));
    }
    Refilter();
    if (!keepId.empty())
    {
        for (size_t i = 0; i < visible_.size(); ++i)
            if (visible_[i]->profileId + "|" + visible_[i]->title == keepId)
                selected_ = static_cast<int>(i);
    }
}

void PickerModel::Refilter()
{
    visible_.clear();
    for (const auto &it : items_)
        if (Matches(it, filter_))
            visible_.push_back(&it);
    if (visible_.empty())
        selected_ = -1;
    else if (selected_ < 0 || selected_ >= static_cast<int>(visible_.size()))
        selected_ = 0;
}

const Item *PickerModel::SelectedItem() const
{
    if (selected_ < 0 || selected_ >= static_cast<int>(visible_.size()))
        return nullptr;
    return visible_[static_cast<size_t>(selected_)];
}

bool PickerModel::FilterIsQuickTarget() const
{
    if (filter_.find(' ') != std::string::npos)
        return false;
    if (filter_.find('@') == std::string::npos && filter_.find(':') == std::string::npos)
        return false;
    std::string error;
    return cli::ParseTarget(filter_, &error).has_value();
}

void PickerModel::Move(int delta)
{
    if (visible_.empty())
        return;
    const int last = static_cast<int>(visible_.size()) - 1;
    selected_ = std::clamp(selected_ + delta, 0, last);
}

Action PickerModel::Feed(std::string_view keys)
{
    const auto split = SplitKeys(keys);
    for (size_t i = 0; i < split.size(); ++i)
    {
        Action a = Key(split[i]);
        if (a.kind != ActionKind::None)
        {
            for (size_t j = i + 1; j < split.size(); ++j)
                a.rest += split[j];
            return a;
        }
    }
    return {};
}

Action PickerModel::Key(std::string_view k)
{
    if (confirmDelete_)
    {
        confirmDelete_ = false;
        if ((k == "y" || k == "Y") && SelectedItem())
            return {ActionKind::DeleteProfile, SelectedItem(), {}};
        status_ = "Delete cancelled.";
        return {};
    }
    status_.clear();

    if (k == "\x1b" || k == "\x03") // Esc, Ctrl+C
    {
        if (!filter_.empty() && k == "\x1b")
        {
            filter_.clear(); // first Esc clears the filter
            Refilter();
            return {};
        }
        return {ActionKind::Quit, nullptr, {}};
    }
    if (k == "\r" || k == "\n")
    {
        if (FilterIsQuickTarget() && (visible_.empty() || filter_.find('@') != std::string::npos))
            return {ActionKind::QuickConnect, nullptr, filter_};
        if (const Item *it = SelectedItem())
            return {ActionKind::Connect, it, {}};
        if (!filter_.empty())
            status_ = "No match. Type user@host to connect to a new host.";
        return {};
    }
    if (k == "\x1b[A" || k == "\x1bOA" || k == "\x10") // Up, Ctrl+P
    {
        Move(-1);
        return {};
    }
    if (k == "\x1b[B" || k == "\x1bOB") // Down (Ctrl+N is "new profile", not down)
    {
        Move(1);
        return {};
    }
    if (k == "\x1b[5~")
    {
        Move(-pageSize_);
        return {};
    }
    if (k == "\x1b[6~")
    {
        Move(pageSize_);
        return {};
    }
    if (k == "\x1b[H" || k == "\x1bOH" || k == "\x1b[1~")
    {
        Move(-1000000);
        return {};
    }
    if (k == "\x1b[F" || k == "\x1bOF" || k == "\x1b[4~")
    {
        Move(1000000);
        return {};
    }
    if (k == "\x0e") // Ctrl+N
        return {ActionKind::NewProfile, nullptr, {}};
    if (k == "\x05" || k == "\x1bOQ" || k == "\x1b[12~") // Ctrl+E, F2
    {
        const Item *it = SelectedItem();
        if (it && it->kind == Item::Kind::Profile)
            return {ActionKind::EditProfile, it, {}};
        status_ = "Select a profile to edit (Ctrl+S saves a recent connection as a profile).";
        return {};
    }
    if (k == "\x13") // Ctrl+S
    {
        const Item *it = SelectedItem();
        if (it && it->kind == Item::Kind::Recent)
            return {ActionKind::SaveRecent, it, {}};
        status_ = "Select a recent connection to save it as a profile.";
        return {};
    }
    if (k == "\x1b[3~") // Delete
    {
        const Item *it = SelectedItem();
        if (it && it->kind == Item::Kind::Profile)
        {
            confirmDelete_ = true;
            status_ = "Delete profile '" + it->profileId + "' and its saved password? (y/N)";
        }
        else
        {
            status_ = "Only profiles can be deleted.";
        }
        return {};
    }
    if (k == "\x7f" || k == "\b") // Backspace
    {
        PopChar(filter_);
        Refilter();
        return {};
    }
    if (k == "\x15") // Ctrl+U: clear filter
    {
        filter_.clear();
        Refilter();
        return {};
    }
    // Printable text (including UTF-8) goes to the filter; other controls and sequences are ignored.
    const unsigned char c0 = static_cast<unsigned char>(k[0]);
    if (c0 >= 0x20 && c0 != 0x7f && k[0] != '\x1b')
    {
        filter_.append(k);
        selected_ = 0;
        Refilter();
    }
    return {};
}

} // namespace winssh::picker
