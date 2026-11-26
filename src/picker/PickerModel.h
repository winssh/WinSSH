// Connection picker state: Profiles + Recent lists, filter, selection, key handling.
// Pure logic (no console I/O) so it can be unit tested; PickerView draws it.
#pragma once

#include "config/Profiles.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winssh::picker
{

struct Item
{
    enum class Kind
    {
        Profile,
        Recent,
    } kind = Kind::Profile;
    std::string title;  // profile name, or user@host[:port]
    std::string detail; // user@host[:port] for profiles, last connected for recent
    std::string tags;   // e.g. "[default,journal]"
    std::string profileId;      // profile, or recent entry made with a profile
    config::RecentEntry recent; // for Kind::Recent
};

enum class ActionKind
{
    None,          // keep running (redraw)
    Connect,       // selected item
    QuickConnect,  // filter text is user@host[:port]
    NewProfile,    // Ctrl+N
    EditProfile,   // Ctrl+E / F2 on a profile
    SaveRecent,    // Ctrl+S on a recent entry: new profile pre-filled from it
    DeleteProfile, // Delete then y: delete the selected profile
    Quit,          // Esc / Ctrl+C
};

struct Action
{
    ActionKind kind = ActionKind::None;
    const Item *item = nullptr; // Connect / EditProfile / SaveRecent / DeleteProfile
    std::string text;           // QuickConnect target
    std::string rest;           // keys typed after the one that triggered the action (type-ahead)
};

class PickerModel
{
public:
    void SetData(const std::vector<config::Profile> &profiles, const std::vector<config::RecentEntry> &recent);

    // Feeds keys as they come from the console in VT input mode (UTF-8; arrows as ESC [ A ...).
    // Returns the first action that leaves the picker loop; keys after it are in Action::rest.
    Action Feed(std::string_view keys);

    const std::string &Filter() const { return filter_; }
    const std::vector<const Item *> &Visible() const { return visible_; }
    int Selected() const { return selected_; } // index into Visible(), -1 when empty
    const Item *SelectedItem() const;
    bool ConfirmingDelete() const { return confirmDelete_; }
    const std::string &Status() const { return status_; } // one-line message (errors, hints)
    void SetStatus(std::string s) { status_ = std::move(s); }

    // True when the filter text would be a quick connect on Enter (has '@', or ':' with a port).
    bool FilterIsQuickTarget() const;

    // Number of visible items, for paging.
    void SetPageSize(int rows) { pageSize_ = rows > 1 ? rows : 1; }

private:
    void Refilter();
    void Move(int delta);
    Action Key(std::string_view key);

    std::vector<Item> items_;
    std::vector<const Item *> visible_;
    std::string filter_;
    int selected_ = -1;
    int pageSize_ = 10;
    bool confirmDelete_ = false;
    std::string status_;
};

// Splits a key buffer into keys: one UTF-8 character, or one escape sequence (CSI / SS3).
// A lone ESC at the end of the buffer is the Esc key.
std::vector<std::string> SplitKeys(std::string_view keys);

// Case-insensitive "all words appear" match of the filter against the item text.
bool Matches(const Item &item, std::string_view filter);

} // namespace winssh::picker
