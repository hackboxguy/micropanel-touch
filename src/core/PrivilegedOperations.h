#pragma once

#include "core/StaticIpSettings.h"

#include <string>
#include <variant>

namespace micropanel_touch::core {

// A typed request is the only network write the initial privileged broker
// understands. It deliberately contains no executable path, shell text, or
// connection-profile identifier supplied by the UI.
struct StaticIpv4Operation {
    std::string interface_name;
    StaticIpSettings settings;
};

// DHCP is a separate operation, rather than a "static" request with empty
// fields, so the broker can keep both wire shapes and handlers allowlisted.
struct DhcpOperation {
    std::string interface_name;
};

// Keep DHCP server separate from DHCP client and static IPv4.  This lets the
// broker validate the complete lease range and map it to one allowlisted
// handler without accepting arbitrary dnsmasq configuration from the UI.
struct DhcpServerOperation {
    std::string interface_name;
    DhcpServerSettings settings;
};

using NetworkOperation = std::variant<StaticIpv4Operation, DhcpOperation, DhcpServerOperation>;

struct PrivilegedOperationReply {
    bool ok{false};
    std::string message;
};

StaticIpValidationResult validate_static_ipv4_operation(const StaticIpv4Operation& operation);
StaticIpValidationResult validate_dhcp_operation(const DhcpOperation& operation);
StaticIpValidationResult validate_dhcp_server_operation(const DhcpServerOperation& operation);
StaticIpValidationResult validate_network_operation(const NetworkOperation& operation);

}  // namespace micropanel_touch::core
