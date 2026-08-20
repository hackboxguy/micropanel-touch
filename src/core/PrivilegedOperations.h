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
// Stage 4. Like "usb", this names a source *kind*, not a location: the
// release URL is pinned in the image and the client cannot influence it.
inline constexpr std::string_view kSystemUpdateOtaSource{"ota"};
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

// Asking whether a release is available carries nothing either: which server
// to ask, and which key to trust, are both pinned in the image. The reply says
// only whether an update is offered and which version it is.
struct CheckSystemUpdateOperation {};

// Reboot and shutdown. The client supplies an enum and nothing else - not a
// command, not a systemd target, not a delay. That is the whole point of the
// type: the two words below are the complete vocabulary of what an
// unprivileged UI can ask the root broker to do to this machine's power state,
// and adding a third means editing this enum, the broker's parser, and the
// handler's case statement together.
enum class PowerAction {
    reboot,
    shutdown,
};

struct PowerOperation {
    PowerAction action{PowerAction::reboot};
};

// The wire spelling, in one place, so the client and the broker cannot drift.
std::string_view power_action_name(PowerAction action);
bool parse_power_action(std::string_view name, PowerAction* action);

// Factory reset carries nothing at all: the request *is* the whole message.
// There is no path, no target and no option for a client to influence - the
// engine wipes the durable state it is configured with, or nothing.
struct FactoryResetOperation {};

using NetworkOperation = std::variant<StaticIpv4Operation, DhcpOperation, DhcpServerOperation>;
using PrivilegedOperation = std::variant<StaticIpv4Operation, DhcpOperation, DhcpServerOperation,
                                         SystemUpdateOperation, CheckSystemUpdateOperation,
                                         FactoryResetOperation, PowerOperation>;

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
