#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace micropanel_touch::platform {

constexpr unsigned int kDisplayBrightnessMinimumPercent = 5U;
constexpr unsigned int kDisplayBrightnessMaximumPercent = 100U;

struct DisplayBrightnessSettings {
    unsigned int percent{100U};
};

bool display_brightness_settings_are_valid(const DisplayBrightnessSettings& settings);
std::optional<DisplayBrightnessSettings> load_display_brightness_settings(
    const std::filesystem::path& path, std::string* diagnostic = nullptr);
bool save_display_brightness_settings(const std::filesystem::path& path,
                                      const DisplayBrightnessSettings& settings,
                                      std::string* diagnostic = nullptr);

}  // namespace micropanel_touch::platform
