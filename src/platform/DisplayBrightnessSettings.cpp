#include "platform/DisplayBrightnessSettings.h"
#include "platform/SettingsFile.h"

#include <array>
#include <charconv>
#include <string_view>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

constexpr int kFormatVersion = 1;
constexpr std::size_t kMaximumFileBytes = 96U;
constexpr std::array<std::string_view, 2> kKeys{"version", "percent"};

void set_diagnostic(std::string* diagnostic, std::string message) {
    if (diagnostic != nullptr) {
        *diagnostic = std::move(message);
    }
}

bool parse_integer(std::string_view text, int* value) {
    if (text.empty()) {
        return false;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), *value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

}  // namespace

bool display_brightness_settings_are_valid(const DisplayBrightnessSettings& settings) {
    return settings.percent >= kDisplayBrightnessMinimumPercent &&
           settings.percent <= kDisplayBrightnessMaximumPercent;
}

std::optional<DisplayBrightnessSettings> load_display_brightness_settings(
    const fs::path& path, std::string* diagnostic) {
    SettingsFileError error = SettingsFileError::None;
    const auto values = load_settings_file(path, kMaximumFileBytes, kKeys.data(), kKeys.size(),
                                           0640, &error);
    if (!values.has_value()) {
        if (error == SettingsFileError::Missing) {
            return std::nullopt;
        }
        if (error == SettingsFileError::InvalidLine) {
            set_diagnostic(diagnostic, "display brightness settings file contains an invalid line");
        } else if (error == SettingsFileError::UnknownOrRepeatedKey) {
            set_diagnostic(diagnostic,
                           "display brightness settings file contains an unknown or repeated key");
        } else if (error == SettingsFileError::Metadata) {
            set_diagnostic(diagnostic, "display brightness settings file is invalid");
        } else if (error == SettingsFileError::Incomplete) {
            set_diagnostic(diagnostic, "display brightness settings file is unsupported");
        } else {
            set_diagnostic(diagnostic, "unable to read display brightness settings");
        }
        return std::nullopt;
    }
    int version = 0;
    int percent = 0;
    if (!parse_integer(values->at("version"), &version) ||
        !parse_integer(values->at("percent"), &percent) || version != kFormatVersion ||
        percent < 0) {
        set_diagnostic(diagnostic, "display brightness settings file is unsupported");
        return std::nullopt;
    }
    const DisplayBrightnessSettings settings{static_cast<unsigned int>(percent)};
    if (!display_brightness_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "display brightness settings are outside the supported range");
        return std::nullopt;
    }
    return settings;
}

bool save_display_brightness_settings(const fs::path& path,
                                      const DisplayBrightnessSettings& settings,
                                      std::string* diagnostic) {
    if (path.empty() || path.parent_path().empty() || !display_brightness_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "display brightness settings are invalid");
        return false;
    }
    const std::string content = "version=" + std::to_string(kFormatVersion) + "\n" +
                                "percent=" + std::to_string(settings.percent) + "\n";
    SettingsFileError error = SettingsFileError::None;
    if (!save_settings_file(path, content, 0640, &error)) {
        if (error == SettingsFileError::Create) {
            set_diagnostic(diagnostic, "unable to create display brightness settings");
        } else if (error == SettingsFileError::Write) {
            set_diagnostic(diagnostic, "unable to write display brightness settings");
        } else if (error == SettingsFileError::Replace) {
            set_diagnostic(diagnostic, "unable to replace display brightness settings");
        } else {
            set_diagnostic(diagnostic, "unable to sync display brightness settings directory");
        }
        return false;
    }
    return true;
}

}  // namespace micropanel_touch::platform
