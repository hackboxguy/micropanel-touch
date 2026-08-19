#pragma once

#include "core/StaticIpSettings.h"

#include <string>
#include <string_view>
#include <variant>

namespace micropanel_touch::core {

// Stage 2b: the client no longer names a filesystem path at all. It names a
// source kind, and the root handler discovers the media itself. Keep the
// published bundle extension here so the UI instruction and the handler's
// discovery rule cannot drift apart.
inline constexpr std::string_view kSystemUpdateUsbSource{"usb"};
inline constexpr std::string_view kSystemUpdateBundleExtension{".mpupdate"};

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

// The UI can supply only a source kind.  The root handler owns every
// executable, block device, mount option, manifest check, and selector
// transition; it never accepts a command, a path, or a target from the client.
// Stage 4 adds a second enum value for the pinned OTA URL template, which is
// likewise never client-supplied.
struct SystemUpdateOperation {
    std::string source;
};

// Factory reset carries nothing at all: the request *is* the whole message.
// There is no path, no target and no option for a client to influence - the
// engine wipes the durable state it is configured with, or nothing.
struct FactoryResetOperation {};

using NetworkOperation = std::variant<StaticIpv4Operation, DhcpOperation, DhcpServerOperation>;
using PrivilegedOperation = std::variant<StaticIpv4Operation, DhcpOperation, DhcpServerOperation,
                                         SystemUpdateOperation, FactoryResetOperation>;

struct PrivilegedOperationReply {
    bool ok{false};
    std::string message;
};

StaticIpValidationResult validate_static_ipv4_operation(const StaticIpv4Operation& operation);
StaticIpValidationResult validate_dhcp_operation(const DhcpOperation& operation);
StaticIpValidationResult validate_dhcp_server_operation(const DhcpServerOperation& operation);
StaticIpValidationResult validate_network_operation(const NetworkOperation& operation);
StaticIpValidationResult validate_system_update_operation(const SystemUpdateOperation& operation);

}  // namespace micropanel_touch::core
