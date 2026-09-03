#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace micropanel_touch::platform {

// What the panel remembers about the IoT agent so the IOT-Agent screen can
// show it again: the account and its connection options, whether the owner
// left it connected, and deliberately not the password. The password lives
// only in the agent's own root-owned login file, which the HMI account cannot
// read - the screen asks for it again whenever it applies a change.
struct IotAgentSettings {
    std::string user;
    std::string server;
    unsigned int port{0U};  // 0: default
    bool bosh{false};
    std::string bosh_url;
    std::string bosh_host;
    std::string admin;
    bool enabled{true};     // false after Disconnect

    bool same_account_as(const IotAgentSettings& other) const {
        return user == other.user && server == other.server && port == other.port &&
               bosh == other.bosh && bosh_url == other.bosh_url && bosh_host == other.bosh_host &&
               admin == other.admin;
    }
};

bool iot_agent_settings_are_valid(const IotAgentSettings& settings);
std::optional<IotAgentSettings> load_iot_agent_settings(const std::filesystem::path& path,
                                                        std::string* diagnostic = nullptr);
bool save_iot_agent_settings(const std::filesystem::path& path, const IotAgentSettings& settings,
                             std::string* diagnostic = nullptr);

}  // namespace micropanel_touch::platform
