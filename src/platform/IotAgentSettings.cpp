#include "platform/IotAgentSettings.h"
#include "core/PrivilegedOperations.h"
#include "platform/SettingsFile.h"

#include <array>
#include <charconv>
#include <string_view>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

constexpr int kFormatVersion = 1;
constexpr std::size_t kMaximumFileBytes = 1024U;
constexpr std::array<std::string_view, 3> kKeys{"version", "user", "server"};
// The settings grammar has no empty values, so "no server override" is
// spelled with a token no host name can be (a label cannot start with '-').
constexpr std::string_view kNoServer{"-"};

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

bool iot_agent_settings_are_valid(const IotAgentSettings& settings) {
    // The same rules the broker applies to a request, with a placeholder
    // password so the account and server are what is being judged.
    return core::validate_iot_agent_config_operation(
               core::IotAgentConfigOperation{settings.user, settings.server, "x"})
        .valid;
}

std::optional<IotAgentSettings> load_iot_agent_settings(const fs::path& path,
                                                        std::string* diagnostic) {
    SettingsFileError error = SettingsFileError::None;
    const auto values = load_settings_file(path, kMaximumFileBytes, kKeys.data(), kKeys.size(),
                                           0640, &error);
    if (!values.has_value()) {
        if (error == SettingsFileError::Missing) {
            return std::nullopt;
        }
        if (error == SettingsFileError::InvalidLine) {
            set_diagnostic(diagnostic, "IoT agent settings file contains an invalid line");
        } else if (error == SettingsFileError::UnknownOrRepeatedKey) {
            set_diagnostic(diagnostic,
                           "IoT agent settings file contains an unknown or repeated key");
        } else if (error == SettingsFileError::Incomplete) {
            set_diagnostic(diagnostic, "IoT agent settings file is unsupported");
        } else if (error == SettingsFileError::Metadata) {
            set_diagnostic(diagnostic, "IoT agent settings file is invalid");
        } else {
            set_diagnostic(diagnostic, "unable to read IoT agent settings");
        }
        return std::nullopt;
    }
    int version = 0;
    if (!parse_integer(values->at("version"), &version) || version != kFormatVersion) {
        set_diagnostic(diagnostic, "IoT agent settings file is unsupported");
        return std::nullopt;
    }
    const std::string& server = values->at("server");
    const IotAgentSettings settings{values->at("user"),
                                    server == kNoServer ? std::string{} : server};
    if (!iot_agent_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "IoT agent settings file names an invalid account");
        return std::nullopt;
    }
    return settings;
}

bool save_iot_agent_settings(const fs::path& path, const IotAgentSettings& settings,
                             std::string* diagnostic) {
    if (path.empty() || path.parent_path().empty() || !iot_agent_settings_are_valid(settings)) {
        set_diagnostic(diagnostic, "IoT agent settings are invalid");
        return false;
    }
    const std::string content = "version=" + std::to_string(kFormatVersion) + "\n" +
                                "user=" + settings.user + "\n" + "server=" +
                                (settings.server.empty() ? std::string(kNoServer) : settings.server) +
                                "\n";
    SettingsFileError error = SettingsFileError::None;
    if (!save_settings_file(path, content, 0640, &error)) {
        if (error == SettingsFileError::Create) {
            set_diagnostic(diagnostic, "unable to create IoT agent settings");
        } else if (error == SettingsFileError::Write) {
            set_diagnostic(diagnostic, "unable to write IoT agent settings");
        } else if (error == SettingsFileError::Replace) {
            set_diagnostic(diagnostic, "unable to replace IoT agent settings");
        } else {
            set_diagnostic(diagnostic, "unable to sync IoT agent settings directory");
        }
        return false;
    }
    return true;
}

}  // namespace micropanel_touch::platform
