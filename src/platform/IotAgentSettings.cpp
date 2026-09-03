#include "platform/IotAgentSettings.h"
#include "core/PrivilegedOperations.h"
#include "platform/SettingsFile.h"

#include <array>
#include <charconv>
#include <string_view>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

constexpr int kFormatVersion = 2;
constexpr std::size_t kMaximumFileBytes = 2048U;
constexpr std::array<std::string_view, 9> kKeys{"version",  "user",      "server", "port", "bosh",
                                                "bosh_url", "bosh_host", "admin",  "enabled"};
// The settings grammar has no empty values, so "not set" is spelled with a
// token no host name, URL or JID can be (none may start with '-').
constexpr std::string_view kNone{"-"};

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

std::string or_none(const std::string& value) {
    return value.empty() ? std::string(kNone) : value;
}

std::string from_none(const std::string& value) {
    return value == kNone ? std::string{} : value;
}

}  // namespace

bool iot_agent_settings_are_valid(const IotAgentSettings& settings) {
    // The same rules the broker applies to a request, with a placeholder
    // password so everything but the password is what is being judged.
    core::IotAgentConfigOperation operation;
    operation.user = settings.user;
    operation.server = settings.server;
    operation.port = settings.port;
    operation.bosh = settings.bosh;
    operation.bosh_url = settings.bosh_url;
    operation.bosh_host = settings.bosh_host;
    operation.admin = settings.admin;
    operation.password = "x";
    return core::validate_iot_agent_config_operation(operation).valid;
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
            // Version 1 held the account and server only. Carry them over
            // with the defaults for everything else, so an owner who set the
            // account up on the first release does not type it again.
            static constexpr std::array<std::string_view, 3> kKeysV1{"version", "user", "server"};
            SettingsFileError v1_error = SettingsFileError::None;
            const auto v1 = load_settings_file(path, kMaximumFileBytes, kKeysV1.data(),
                                               kKeysV1.size(), 0640, &v1_error);
            int v1_version = 0;
            if (v1.has_value() && parse_integer(v1->at("version"), &v1_version) &&
                v1_version == 1) {
                IotAgentSettings settings;
                settings.user = v1->at("user");
                settings.server = from_none(v1->at("server"));
                if (iot_agent_settings_are_valid(settings)) {
                    return settings;
                }
            }
            set_diagnostic(diagnostic, "IoT agent settings file is unsupported");
        } else if (error == SettingsFileError::Metadata) {
            set_diagnostic(diagnostic, "IoT agent settings file is invalid");
        } else {
            set_diagnostic(diagnostic, "unable to read IoT agent settings");
        }
        return std::nullopt;
    }
    int version = 0;
    int port = 0;
    int bosh = 0;
    int enabled = 0;
    if (!parse_integer(values->at("version"), &version) || version != kFormatVersion ||
        !parse_integer(values->at("port"), &port) || port < 0 || port > 65535 ||
        !parse_integer(values->at("bosh"), &bosh) || (bosh != 0 && bosh != 1) ||
        !parse_integer(values->at("enabled"), &enabled) || (enabled != 0 && enabled != 1)) {
        set_diagnostic(diagnostic, "IoT agent settings file is unsupported");
        return std::nullopt;
    }
    IotAgentSettings settings;
    settings.user = values->at("user");
    settings.server = from_none(values->at("server"));
    settings.port = static_cast<unsigned int>(port);
    settings.bosh = bosh == 1;
    settings.bosh_url = from_none(values->at("bosh_url"));
    settings.bosh_host = from_none(values->at("bosh_host"));
    settings.admin = from_none(values->at("admin"));
    settings.enabled = enabled == 1;
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
    const std::string content =
        "version=" + std::to_string(kFormatVersion) + "\n" + "user=" + settings.user + "\n" +
        "server=" + or_none(settings.server) + "\n" + "port=" + std::to_string(settings.port) +
        "\n" + "bosh=" + std::string(settings.bosh ? "1" : "0") + "\n" +
        "bosh_url=" + or_none(settings.bosh_url) + "\n" +
        "bosh_host=" + or_none(settings.bosh_host) + "\n" + "admin=" + or_none(settings.admin) +
        "\n" + "enabled=" + std::string(settings.enabled ? "1" : "0") + "\n";
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
