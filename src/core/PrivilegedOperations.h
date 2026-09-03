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

// Joining a hotspot. This is the one privileged request that carries a secret,
// and everything about its shape follows from that:
//
//   - The passphrase is in the request because it has to be, and nowhere else.
//     It never reaches an argument vector, a log line, a UI event, a broker
//     reply, or a control capture.
//   - There is exactly one saved Wi-Fi profile, under a fixed name. The client
//     therefore never influences a path, which is the same rule every other
//     operation here follows; a per-SSID filename would hand it one.
//   - An empty passphrase means an open network, deliberately spelled as
//     "empty" rather than as a separate operation: the two differ in one
//     field, and a second operation would be a second code path to keep
//     honest about redaction.
struct WifiJoinOperation {
    std::string ssid;
    std::string passphrase;
};

// Forgetting it again. Carries no name: there is only one saved profile, so
// naming it would be a client-supplied selector for no gain.
struct WifiForgetOperation {};

// Connecting or disconnecting the profile that is already saved - the two
// actions that need no password, because the panel already has one.
//
// Disconnect also clears autoconnect, and that is the whole reason it is a
// distinct action rather than a bare "down": the profile exists precisely so
// NetworkManager rejoins on its own, so a disconnect that left autoconnect
// alone would be a button that appears to do nothing. Connect sets it back.
// This mirrors what a phone does with "disconnect" versus "forget".
enum class WifiProfileAction {
    connect,
    disconnect,
};

struct WifiProfileOperation {
    WifiProfileAction action{WifiProfileAction::connect};
};

std::string_view wifi_profile_action_name(WifiProfileAction action);
bool parse_wifi_profile_action(std::string_view name, WifiProfileAction* action);

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

// The IoT agent's XMPP account. xmproxysrv (the jsonrpc-tcp-srv XMPP
// endpoint) reads it from a root-owned login file the HMI account cannot
// touch, so the panel hands the values to the broker and the handler rewrites
// that file and restarts the agent.
//
// The password follows every rule the Wi-Fi passphrase does: it is in the
// request because it must be, it travels to the handler on stdin, and no
// diagnostic ever quotes it. `server` is the optional host override the login
// file calls `server:`; empty means "the domain of the JID", which is what
// the agent does on its own. `port` 0 means the default (5222). `bosh` tunnels
// the session over HTTP(S) at `bosh_url`, with `bosh_host` as the optional
// Host header override. `admin` is the optional admin buddy (`adminbuddy:`);
// empty leaves whatever the file already says.
struct IotAgentConfigOperation {
    std::string user;      // bare JID, local@domain
    std::string server;    // optional host override
    std::string password;
    unsigned int port{0U};  // 0: default
    bool bosh{false};
    std::string bosh_url;   // http(s)://host[:port]/path, required when bosh
    std::string bosh_host;  // optional
    std::string admin;      // optional bare JID
};

// Starting and stopping the agent that is already configured. Stop is
// remembered by the handler (a marker the unit's ConditionPathExists reads),
// so a panel whose owner pressed Disconnect stays disconnected across a
// reboot; start removes the marker and restarts the unit.
enum class IotAgentControlAction {
    start,
    stop,
};

struct IotAgentControlOperation {
    IotAgentControlAction action{IotAgentControlAction::start};
};

std::string_view iot_agent_control_action_name(IotAgentControlAction action);
bool parse_iot_agent_control_action(std::string_view name, IotAgentControlAction* action);

// Factory reset carries nothing at all: the request *is* the whole message.
// There is no path, no target and no option for a client to influence - the
// engine wipes the durable state it is configured with, or nothing.
struct FactoryResetOperation {};

// Wi-Fi joins ride the same asynchronous apply path as the wired settings.
// That is not a convenience: the path already owns the pending state, the
// result card, the blocked Back and the one-request-at-a-time rule, and a
// second path would have to earn all four again.
using NetworkOperation = std::variant<StaticIpv4Operation, DhcpOperation, DhcpServerOperation,
                                      WifiJoinOperation, WifiForgetOperation,
                                      WifiProfileOperation>;
using PrivilegedOperation = std::variant<StaticIpv4Operation, DhcpOperation, DhcpServerOperation,
                                         SystemUpdateOperation, CheckSystemUpdateOperation,
                                         FactoryResetOperation, PowerOperation,
                                         WifiJoinOperation, WifiForgetOperation,
                                         WifiProfileOperation, IotAgentConfigOperation,
                                         IotAgentControlOperation>;

struct PrivilegedOperationReply {
    bool ok{false};
    std::string message;
};

StaticIpValidationResult validate_static_ipv4_operation(const StaticIpv4Operation& operation);
StaticIpValidationResult validate_dhcp_operation(const DhcpOperation& operation);
StaticIpValidationResult validate_dhcp_server_operation(const DhcpServerOperation& operation);
StaticIpValidationResult validate_network_operation(const NetworkOperation& operation);
StaticIpValidationResult validate_system_update_operation(const SystemUpdateOperation& operation);
// Never quotes the passphrase, or its length, in the returned message: a
// diagnostic that says "the passphrase 'hunter2' is too short" has published
// the secret to every surface a diagnostic reaches.
StaticIpValidationResult validate_wifi_join_operation(const WifiJoinOperation& operation);

// Never quotes the password, or its length. The login file is parsed as
// whitespace-separated "key: value" lines, so a value with whitespace would be
// silently truncated by the agent - the validator refuses it instead.
StaticIpValidationResult validate_iot_agent_config_operation(
    const IotAgentConfigOperation& operation);

// The single saved profile's identity, shared by the broker and the handler so
// they cannot drift. It is not a path: the handler owns the directory.
inline constexpr std::string_view kWifiProfileId{"micropanel-touch-wifi"};

}  // namespace micropanel_touch::core
