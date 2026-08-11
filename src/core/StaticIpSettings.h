#pragma once

#include <optional>
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

// The privileged NetworkManager contract uses CIDR prefix lengths, while the
// touch screen intentionally asks for the familiar dotted netmask notation.
// Return no value for malformed or non-contiguous masks such as 255.0.255.0.
std::optional<std::string> prefix_length_from_ipv4_netmask(const std::string& netmask);

}  // namespace micropanel_touch::core
