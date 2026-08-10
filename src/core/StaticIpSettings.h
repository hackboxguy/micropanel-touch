#pragma once

#include <string>

namespace micropanel_touch::core {

struct StaticIpSettings {
    std::string address;
    std::string prefix_length;
    std::string gateway;
};

struct StaticIpValidationResult {
    bool valid{false};
    std::string message;
};

/**
 * Validate the values collected by the touch UI before they ever reach a
 * network-management command. Applying settings remains a separate,
 * privileged operation.
 */
StaticIpValidationResult validate_static_ipv4(const StaticIpSettings& settings);

}  // namespace micropanel_touch::core
