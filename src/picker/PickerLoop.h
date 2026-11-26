// Runs the picker on the console until an action leaves it.
#pragma once

#include "picker/PickerModel.h"
#include "picker/PickerView.h"

namespace winssh::picker
{

// Draws the picker and reads keys (console must be in raw VT input mode, alternate screen entered).
// Redraws on every key and on window resize. The returned Action's item points into `model`
// and stays valid until model.SetData() is called again.
Action RunPickerLoop(PickerModel &model, PickerView &view);

// Puts keys back into the console input queue (e.g. Action::rest before connecting), so the
// next reader gets what the user typed ahead.
void PushBackKeys(const std::string &utf8);

} // namespace winssh::picker
