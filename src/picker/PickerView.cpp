#include "picker/PickerView.h"

#include <algorithm>

namespace winssh::picker
{
namespace
{

constexpr const char *kReset = "\x1b[0m";
constexpr const char *kDim = "\x1b[2m";
constexpr const char *kBold = "\x1b[1m";
constexpr const char *kReverse = "\x1b[7m";
constexpr const char *kYellow = "\x1b[33m";
constexpr const char *kCyan = "\x1b[36m";

size_t CodePoints(const std::string &s)
{
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80)
            ++n;
    return n;
}

std::string Pad(const std::string &text, size_t width)
{
    std::string out = FitWidth(text, width);
    const size_t n = CodePoints(out);
    if (n < width)
        out.append(width - n, ' ');
    return out;
}

// One screen row of the list: a section header or an item.
struct Row
{
    const Item *item = nullptr; // null = section header
    std::string header;
    int index = -1; // index into Visible() for items
};

} // namespace

std::string FitWidth(const std::string &text, size_t width)
{
    if (CodePoints(text) <= width)
        return text;
    if (width == 0)
        return {};
    std::string out;
    size_t n = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c & 0xC0) != 0x80)
        {
            if (n + 1 == width)
                break;
            ++n;
        }
        out += text[i];
    }
    return out + "\xE2\x80\xA6"; // …
}

std::string PickerView::Render(PickerModel &model, int cols, int rows)
{
    cols = std::max(cols, 20);
    rows = std::max(rows, 8);
    const size_t width = static_cast<size_t>(cols);
    std::string out = "\x1b[H"; // home; every line ends with "erase to end of line"
    int line = 0;
    auto emit = [&](const std::string &text) {
        if (line >= rows)
            return;
        out += text;
        out += kReset;
        out += "\x1b[K";
        if (++line < rows)
            out += "\r\n";
    };

    // Header: title on the left, filter on the right.
    const std::string title = " WinSSH \xE2\x80\x94 select a connection";
    const std::string filter = "Filter: " + model.Filter() + "_";
    if (CodePoints(title) + CodePoints(filter) + 3 <= width)
        emit(std::string(kBold) + title + kReset +
             std::string(width - CodePoints(title) - CodePoints(filter) - 1, ' ') + kCyan + filter);
    else
        emit(std::string(kBold) + FitWidth(title, width) + kReset);
    if (CodePoints(title) + CodePoints(filter) + 3 > width)
        emit(std::string(kCyan) + " " + FitWidth(filter, width - 1));
    emit("");

    // Build list rows with section headers.
    std::vector<Row> list;
    const auto &visible = model.Visible();
    Item::Kind section = Item::Kind::Profile;
    bool sectionShown = false;
    for (size_t i = 0; i < visible.size(); ++i)
    {
        if (!sectionShown || visible[i]->kind != section)
        {
            section = visible[i]->kind;
            sectionShown = true;
            list.push_back({nullptr, section == Item::Kind::Profile ? "PROFILES" : "RECENT", -1});
        }
        list.push_back({visible[i], {}, static_cast<int>(i)});
    }

    // Footer lines (status, quick-connect hint, key help) are reserved at the bottom.
    std::vector<std::string> footer;
    if (model.FilterIsQuickTarget())
        footer.push_back(std::string(kCyan) + " Enter: connect to " + FitWidth(model.Filter(), width - 21));
    if (!model.Status().empty())
        footer.push_back(std::string(kYellow) + " " + FitWidth(model.Status(), width - 1));
    footer.push_back(std::string(kDim) +
                     FitWidth(" \xE2\x86\x91/\xE2\x86\x93 move  Enter connect  type to filter  Ctrl+N new  "
                              "Ctrl+E edit  Ctrl+S save recent  Del delete  Esc quit",
                              width));

    const int listRows = std::max(1, rows - line - 1 - static_cast<int>(footer.size()));
    model.SetPageSize(std::max(1, listRows - 2));

    if (list.empty())
    {
        if (model.Filter().empty())
        {
            emit("  No profiles or recent connections yet.");
            emit("  Type user@host and press Enter to connect, or press Ctrl+N to create a profile.");
        }
        else if (!model.FilterIsQuickTarget())
        {
            emit("  No match.");
        }
    }
    else
    {
        // Scroll so the selected row is visible.
        int selRow = 0;
        for (size_t r = 0; r < list.size(); ++r)
            if (list[r].index == model.Selected())
                selRow = static_cast<int>(r);
        if (selRow < top_)
            top_ = std::max(0, selRow - 1); // show the section header above when scrolling up
        if (selRow >= top_ + listRows)
            top_ = selRow - listRows + 1;
        top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(list.size()) - listRows));

        // Column widths from the visible items.
        size_t titleW = 8, detailW = 8;
        for (const Row &r : list)
            if (r.item)
            {
                titleW = std::max(titleW, CodePoints(r.item->title));
                detailW = std::max(detailW, CodePoints(r.item->detail));
            }
        titleW = std::min<size_t>(titleW, 30);
        detailW = std::min<size_t>(detailW, 32);

        for (int r = top_; r < top_ + listRows && r < static_cast<int>(list.size()); ++r)
        {
            const Row &row = list[static_cast<size_t>(r)];
            if (!row.item)
            {
                emit(std::string(kDim) + "  " + row.header);
                continue;
            }
            const bool sel = row.index == model.Selected();
            std::string text = (sel ? "  > " : "    ") + Pad(row.item->title, titleW) + "  " +
                               Pad(row.item->detail, detailW) + "  " + row.item->tags;
            text = Pad(text, width);
            emit(sel ? std::string(kReverse) + text : text);
        }
    }

    // Blank rows down to the footer, then the footer.
    while (line < rows - static_cast<int>(footer.size()))
        emit("");
    for (const auto &f : footer)
        emit(f);
    out += "\x1b[J";
    return out;
}

} // namespace winssh::picker
