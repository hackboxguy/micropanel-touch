#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace micropanel_touch::platform {

constexpr unsigned int kDisplayStandbyMinimumSeconds = 10U;
constexpr unsigned int kDisplayStandbyMaximumSeconds = 180U;
constexpr unsigned int kDisplayStandbyStepSeconds = 10U;

struct DisplayStandbySettings {
    bool enabled{true};
    unsigned int seconds{60U};
};

bool display_standby_settings_are_valid(const DisplayStandbySettings& settings);
std::optional<DisplayStandbySettings> load_display_standby_settings(
    const std::filesystem::path& path, std::string* diagnostic = nullptr);
bool save_display_standby_settings(const std::filesystem::path& path,
                                   const DisplayStandbySettings& settings,
                                   std::string* diagnostic = nullptr);

}  // namespace micropanel_touch::platform
