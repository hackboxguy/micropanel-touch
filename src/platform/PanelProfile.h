#pragma once

#include "platform/TouchInput.h"

#include <optional>
#include <string_view>
#include <vector>

namespace micropanel_touch::platform {

// A panel profile is deliberately a small evidence-backed bundle. The app
// selects one from the framebuffer geometry and evdev touch capabilities.
// Boot configuration remains image-owned because it happens before the app
// can inspect hardware; the named configurator is informational only and is
// the single source of truth for its overlay lines.
struct PanelProfile {
    std::string_view id;
    int native_width{0};
    int native_height{0};
    TouchTechnology touch_technology{TouchTechnology::resistive_single_touch};
    std::string_view boot_configurator;
    std::optional<std::string_view> backlight_path;
    bool calibration_recommended{false};
};

const std::vector<PanelProfile>& known_panel_profiles();
std::optional<PanelProfile> select_panel_profile(TouchTechnology touch_technology,
                                                 int native_width, int native_height);

}  // namespace micropanel_touch::platform
