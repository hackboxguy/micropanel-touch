#include "platform/PanelProfile.h"

#include <array>

namespace micropanel_touch::platform {
namespace {

constexpr std::array<PanelProfile, 3> kProfiles{{
    // PiScreen's GPIO-22 backlight-enable is binary, not PWM. The boot helper
    // exposes it through gpio-led so standby can use a kernel-owned sysfs path
    // without pretending that this panel supports a brightness percentage.
    {"piscreen-ads7846-portrait", 320, 480, TouchTechnology::resistive_single_touch,
     "enable-piscreen.sh",
     "/sys/class/leds/micropanel-touch-piscreen-backlight/brightness", true},
    {"piscreen-ads7846-landscape", 480, 320, TouchTechnology::resistive_single_touch,
     "enable-piscreen.sh",
     "/sys/class/leds/micropanel-touch-piscreen-backlight/brightness", true},
    // This profile is tied to the image's explicit luckfox-ctp panel variant;
    // a write-only SPI display cannot safely be identified at runtime.
    {"luckfox-ctp-st7796s-gt911-portrait", 320, 480,
     TouchTechnology::capacitive_multitouch,
     "enable-luckfox-ctp.sh",
     "/sys/class/backlight/backlight_pwm/brightness", false},
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
