#pragma once

#include <optional>
#include <string>

namespace micropanel_touch::core {

struct StaticIpSettings {
    std::string address;
    std::string prefix_length;
    std::string gateway;
};

// DHCP server mode owns one isolated IPv4 subnet.  It deliberately does not
// accept a router or DNS value: this first appliance mode serves local-link
// leases only and never implies that the panel provides forwarding/NAT.
struct DhcpServerSettings {
    std::string address;
    std::string prefix_length;
    std::string lease_start;
    std::string lease_end;
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
StaticIpValidationResult validate_dhcp_server_ipv4(const DhcpServerSettings& settings);

// The privileged NetworkManager contract uses CIDR prefix lengths, while the
// touch screen intentionally asks for the familiar dotted netmask notation.
// Return no value for malformed or non-contiguous masks such as 255.0.255.0.
std::optional<std::string> prefix_length_from_ipv4_netmask(const std::string& netmask);

}  // namespace micropanel_touch::core
