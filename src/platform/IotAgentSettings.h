#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace micropanel_touch::platform {

// What the panel remembers about the IoT agent's account so the IOT-Agent
// screen can show it again: the account and the optional server host, and
// deliberately not the password. The password lives only in the agent's own
// root-owned login file, which the HMI account cannot read - the screen asks
// for it again every time it applies a change.
struct IotAgentSettings {
    std::string user;
    std::string server;
};

bool iot_agent_settings_are_valid(const IotAgentSettings& settings);
std::optional<IotAgentSettings> load_iot_agent_settings(const std::filesystem::path& path,
                                                        std::string* diagnostic = nullptr);
bool save_iot_agent_settings(const std::filesystem::path& path, const IotAgentSettings& settings,
                             std::string* diagnostic = nullptr);

}  // namespace micropanel_touch::platform
