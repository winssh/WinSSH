// Draws the picker with VT sequences (alternate screen, one full frame per change).
#pragma once

#include "picker/PickerModel.h"

#include <string>

namespace winssh::picker
{

class PickerView
{
public:
    // Returns the bytes for one frame of `cols` x `rows`. Keeps the selected item on screen
    // (scrolls) and tells the model how many items fit, for PgUp / PgDn.
    std::string Render(PickerModel &model, int cols, int rows);

    static constexpr const char *kEnter = "\x1b[?1049h\x1b[?25l"; // alternate screen, hide cursor
    static constexpr const char *kLeave = "\x1b[0m\x1b[?25h\x1b[?1049l";

private:
    int top_ = 0; // first visible row of the item list
};

// Fits text into `width` terminal columns (one column per code point), adding "…" when cut.
std::string FitWidth(const std::string &text, size_t width);

} // namespace winssh::picker
