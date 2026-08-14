#include "platform/DisplayStandbySettings.h"
#include "platform/SettingsFile.h"

#include <array>
#include <charconv>
#include <string_view>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

constexpr int kFormatVersion = 1;
constexpr std::size_t kMaximumFileBytes = 128U;
constexpr std::array<std::string_view, 3> kKeys{"version", "enabled", "seconds"};

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

bool display_standby_settings_are_valid(const DisplayStandbySettings& settings) {
    return settings.seconds >= kDisplayStandbyMinimumSeconds &&
           settings.seconds <= kDisplayStandbyMaximumSeconds &&
           settings.seconds % kDisplayStandbyStepSeconds == 0U;
}

std::optional<DisplayStandbySettings> load_display_standby_settings(
    const fs::path& path, std::string* diagnostic) {
    SettingsFileError error = SettingsFileError::None;
    const auto values = load_settings_file(path, kMaximumFileBytes, kKeys.data(), kKeys.size(),
                                           0640, &error);
    if (!values.has_value()) {
        if (error == SettingsFileError::Missing) {
            return std::nullopt;
        }
        if (error == SettingsFileError::InvalidLine) {
            set_diagnostic(diagnostic, "display standby settings file contains an invalid line");
        } else if (error == SettingsFileError::UnknownOrRepeatedKey) {
            set_diagnostic(diagnostic,
                           "display standby settings file contains an unknown or repeated key");
        } else if (error == SettingsFileError::Incomplete) {
            set_diagnostic(diagnostic, "display standby settings file is unsupported");
        } else if (error == SettingsFileError::Metadata) {
            set_diagnostic(diagnostic, "display standby settings file is invalid");
        } else {
            set_diagnostic(diagnostic, "unable to read display standby settings");
        }
        return std::nullopt;
    }
    int version = 0;
    int enabled = 0;
    int seconds = 0;
    if (!parse_integer(values->at("version"), &version) ||
        !parse_integer(values->at("enabled"), &enabled) ||
        !parse_integer(values->at("seconds"), &seconds) || version != kFormatVersion ||
        (enabled != 0 && enabled != 1) || seconds < 0) {
        set_diagnostic(diagnostic, "display standby settings file is unsupported");
        return std::nullopt;
    }
    const DisplayStandbySettings settings{enabled == 1, static_cast<unsigned int>(seconds)};
    if (!display_standby_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "display standby settings are outside the supported range");
        return std::nullopt;
    }
    return settings;
}

bool save_display_standby_settings(const fs::path& path, const DisplayStandbySettings& settings,
                                   std::string* diagnostic) {
    if (path.empty() || path.parent_path().empty() || !display_standby_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "display standby settings are invalid");
        return false;
    }
    const std::string content = "version=" + std::to_string(kFormatVersion) + "\n" +
                                "enabled=" + std::to_string(settings.enabled ? 1 : 0) + "\n" +
                                "seconds=" + std::to_string(settings.seconds) + "\n";
    SettingsFileError error = SettingsFileError::None;
    if (!save_settings_file(path, content, 0640, &error)) {
        if (error == SettingsFileError::Create) {
            set_diagnostic(diagnostic, "unable to create display standby settings");
        } else if (error == SettingsFileError::Write) {
            set_diagnostic(diagnostic, "unable to write display standby settings");
        } else if (error == SettingsFileError::Replace) {
            set_diagnostic(diagnostic, "unable to replace display standby settings");
        } else {
            set_diagnostic(diagnostic, "unable to sync display standby settings directory");
        }
        return false;
    }
    return true;
}

}  // namespace micropanel_touch::platform
