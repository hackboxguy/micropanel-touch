#include "platform/PanelProfile.h"

#include <array>

namespace micropanel_touch::platform {
namespace {

constexpr std::array<PanelProfile, 3> kProfiles{{
    // The accepted PiScreen overlay assigns GPIO 22 to its DRM node, but it
    // exposes no verified backlight sysfs control. Do not fight that kernel
    // ownership with raw GPIO; keep the path unset until it is measured.
    {"piscreen-ads7846-portrait", 320, 480, TouchTechnology::resistive_single_touch,
     "enable-piscreen.sh", std::nullopt, true},
    {"piscreen-ads7846-landscape", 480, 320, TouchTechnology::resistive_single_touch,
     "enable-piscreen.sh", std::nullopt, true},
    // This profile is tied to the image's explicit luckfox-ctp panel variant;
    // a write-only SPI display cannot safely be identified at runtime.
    {"luckfox-ctp-st7796s-gt911-portrait", 320, 480,
     TouchTechnology::capacitive_multitouch,
     "enable-luckfox-ctp.sh",
     "/sys/class/backlight/backlight_gpio/brightness", false},
}};

}  // namespace

const std::vector<PanelProfile>& known_panel_profiles() {
    static const std::vector<PanelProfile> profiles(kProfiles.begin(), kProfiles.end());
    return profiles;
}

std::optional<PanelProfile> select_panel_profile(TouchTechnology touch_technology,
                                                  int native_width, int native_height) {
    for (const PanelProfile& profile : known_panel_profiles()) {
        if (profile.touch_technology == touch_technology &&
            profile.native_width == native_width && profile.native_height == native_height) {
            return profile;
        }
    }
    return std::nullopt;
}

}  // namespace micropanel_touch::platform
