#pragma once

#include <string_view>

namespace micropanel_touch::ui {

// Returns the LVGL symbol for a supported named icon, or nullptr when the
// value is reserved for a future image/path icon implementation.
const char* builtin_icon_symbol(std::string_view name);

}  // namespace micropanel_touch::ui
