#include "core/PrivilegedOperations.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <type_traits>

namespace micropanel_touch::core {
namespace {

bool is_valid_interface_name(const std::string& value) {
    // Linux IFNAMSIZ reserves one byte for NUL. Permit the conventional
    // Ethernet/Wi-Fi/VLAN characters but never a path, whitespace, or a
    // shell/metacharacter.
    if (value.empty() || value.size() > 15U) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '.' || character == '_' ||
               character == '-';
    });
}

}  // namespace

StaticIpValidationResult validate_static_ipv4_operation(const StaticIpv4Operation& operation) {
    if (!is_valid_interface_name(operation.interface_name)) {
        return {false, "Interface name must be 1-15 safe characters."};
    }
    return validate_static_ipv4(operation.settings);
}

StaticIpValidationResult validate_dhcp_operation(const DhcpOperation& operation) {
    if (!is_valid_interface_name(operation.interface_name)) {
        return {false, "Interface name must be 1-15 safe characters."};
    }
    return {true, "DHCP selection is valid; no network changes were made."};
}

StaticIpValidationResult validate_dhcp_server_operation(const DhcpServerOperation& operation) {
    if (!is_valid_interface_name(operation.interface_name)) {
        return {false, "Interface name must be 1-15 safe characters."};
    }
    // The board image contains one dedicated, eth0-bound dnsmasq unit.  Do
    // not allow a panel setting to turn a Wi-Fi or arbitrary VLAN interface
    // into a DHCP authority.
    if (operation.interface_name != "eth0") {
        return {false, "DHCP server mode is supported only on eth0."};
    }
    return validate_dhcp_server_ipv4(operation.settings);
}

StaticIpValidationResult validate_network_operation(const NetworkOperation& operation) {
    return std::visit([](const auto& selected) {
        using Operation = std::decay_t<decltype(selected)>;
        if constexpr (std::is_same_v<Operation, StaticIpv4Operation>) {
            return validate_static_ipv4_operation(selected);
        } else if constexpr (std::is_same_v<Operation, DhcpOperation>) {
            return validate_dhcp_operation(selected);
        } else if constexpr (std::is_same_v<Operation, WifiJoinOperation>) {
            return validate_wifi_join_operation(selected);
        } else if constexpr (std::is_same_v<Operation, WifiForgetOperation>) {
            // Nothing to validate: the request has no fields, and forgetting a
            // network that was never saved is the requested end state.
            return StaticIpValidationResult{true, "Wi-Fi settings are valid; nothing has been applied."};
        } else if constexpr (std::is_same_v<Operation, WifiProfileOperation>) {
            // A closed enum; the broker refuses anything else at the wire.
            return StaticIpValidationResult{true, "Wi-Fi settings are valid; nothing has been applied."};
        } else {
            return validate_dhcp_server_operation(selected);
        }
    }, operation);
}

StaticIpValidationResult validate_system_update_operation(const SystemUpdateOperation& operation) {
    // A closed enum, not a path. The root handler enumerates removable USB
    // media itself, and reads the release URL from its own pinned config, so
    // there is nothing left for a client to point anywhere.
    if (operation.source != kSystemUpdateUsbSource && operation.source != kSystemUpdateOtaSource) {
        return {false, "Update source is not an allowed system update source."};
    }
    return {true, "System update source is valid; no update has been applied."};
}

namespace {

// Control characters would break the keyfile the handler writes and are not
// meaningful in either field. DEL is included because it is a control
// character that happens to sit above the printable range.
bool has_control_characters(std::string_view value) {
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7FU) {
            return true;
        }
    }
    return false;
}

}  // namespace

StaticIpValidationResult validate_wifi_join_operation(const WifiJoinOperation& operation) {
    if (operation.ssid.empty()) {
        return {false, "Select a network first."};
    }
    // 802.11 caps an SSID at 32 octets, and NetworkManager will refuse a
    // longer one anyway - better to say so before the broker is involved.
    if (operation.ssid.size() > 32U) {
        return {false, "That network name is longer than Wi-Fi allows."};
    }
    if (has_control_characters(operation.ssid)) {
        return {false, "That network name contains characters this panel cannot use."};
    }
    if (operation.passphrase.empty()) {
        // An open network. Saying nothing here is deliberate: the caller knows
        // which network it picked, and a "joining without a password" warning
        // belongs on the screen, not in a validation result.
        return {true, "Wi-Fi settings are valid; nothing has been applied."};
    }
    // WPA-PSK: 8 to 63 characters. The message never says what was entered or
    // how long it was.
    if (operation.passphrase.size() < 8U) {
        return {false, "A Wi-Fi password must be at least 8 characters."};
    }
    if (operation.passphrase.size() > 63U) {
        return {false, "A Wi-Fi password can be at most 63 characters."};
    }
    if (has_control_characters(operation.passphrase)) {
        return {false, "That password contains characters this panel cannot use."};
    }
    return {true, "Wi-Fi settings are valid; nothing has been applied."};
}

namespace {

// The agent splits each login-file line on whitespace, so a value that
// contains any is not a value the agent would ever read back whole.
bool has_whitespace(std::string_view value) {
    for (const char character : value) {
        if (character == ' ' || character == '\t') {
            return true;
        }
    }
    return false;
}

// Hostnames and JID domains: letters, digits, '.', '-' and nothing else. This
// is stricter than the RFCs and deliberately so: the value ends up in a file
// a root-owned daemon parses, and a name this rule refuses is a name that was
// almost certainly typed wrong on a 480-pixel keyboard.
bool is_host_name(std::string_view value) {
    if (value.empty() || value.size() > 253U || value.front() == '.' || value.back() == '.' ||
        value.front() == '-') {
        return false;
    }
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        const bool letter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
        const bool digit = byte >= '0' && byte <= '9';
        if (!letter && !digit && character != '.' && character != '-') {
            return false;
        }
    }
    return true;
}

// A bare JID: local@domain, one '@', no resource, no whitespace or control
// characters, and a domain that is a host name.
bool is_bare_jid(std::string_view value) {
    if (value.empty() || value.size() > 255U || has_control_characters(value) ||
        has_whitespace(value)) {
        return false;
    }
    const auto at = value.find('@');
    if (at == std::string_view::npos || at == 0U || at + 1U >= value.size() ||
        value.find('@', at + 1U) != std::string_view::npos ||
        value.find('/') != std::string_view::npos) {
        return false;
    }
    return is_host_name(value.substr(at + 1U));
}

// What the agent's BOSH parser accepts: a scheme, a host with an optional
// port, an optional path. Everything else about the URL is the server's
// business, but whitespace and control characters would break the login file
// exactly as they would anywhere else in it.
bool is_bosh_url(std::string_view value) {
    if (value.size() > 512U || has_control_characters(value) || has_whitespace(value)) {
        return false;
    }
    std::string_view rest;
    if (value.substr(0U, 8U) == "https://") {
        rest = value.substr(8U);
    } else if (value.substr(0U, 7U) == "http://") {
        rest = value.substr(7U);
    } else {
        return false;
    }
    const auto slash = rest.find('/');
    std::string_view host_port = slash == std::string_view::npos ? rest : rest.substr(0U, slash);
    const auto colon = host_port.rfind(':');
    if (colon != std::string_view::npos) {
        const std::string_view port = host_port.substr(colon + 1U);
        if (port.empty() || port.size() > 5U) {
            return false;
        }
        unsigned int number = 0U;
        for (const char character : port) {
            if (character < '0' || character > '9') {
                return false;
            }
            number = number * 10U + static_cast<unsigned int>(character - '0');
        }
        if (number == 0U || number > 65535U) {
            return false;
        }
        host_port = host_port.substr(0U, colon);
    }
    return is_host_name(host_port);
}

}  // namespace

StaticIpValidationResult validate_iot_agent_config_operation(
    const IotAgentConfigOperation& operation) {
    if (operation.user.empty()) {
        return {false, "Enter the agent's account, like bot@example.org."};
    }
    if (!is_bare_jid(operation.user)) {
        return {false, "The account must look like bot@example.org."};
    }
    if (!operation.server.empty() && !is_host_name(operation.server)) {
        return {false, "The server must be a host name, like xmpp.example.org."};
    }
    if (operation.port > 65535U) {
        return {false, "The port must be between 1 and 65535."};
    }
    if (operation.bosh) {
        if (operation.bosh_url.empty()) {
            return {false, "Enter the BOSH URL, like https://xmpp.example.org:5281/http-bind."};
        }
    }
    if (!operation.bosh_url.empty() && !is_bosh_url(operation.bosh_url)) {
        return {false, "The BOSH URL must start with http:// or https:// and name a host."};
    }
    if (!operation.bosh_host.empty() && !is_host_name(operation.bosh_host)) {
        return {false, "The BOSH host must be a host name."};
    }
    if (!operation.admin.empty() && !is_bare_jid(operation.admin)) {
        return {false, "The admin account must look like owner@example.org."};
    }
    if (operation.password.empty()) {
        return {false, "Enter the account's password."};
    }
    // The message never says what was entered or how long it was.
    if (operation.password.size() > 128U) {
        return {false, "That password is longer than this panel supports."};
    }
    if (has_control_characters(operation.password) || has_whitespace(operation.password)) {
        return {false, "That password contains characters this panel cannot use."};
    }
    return {true, "IoT agent settings are valid; nothing has been applied."};
}

std::string_view iot_agent_control_action_name(IotAgentControlAction action) {
    return action == IotAgentControlAction::stop ? "stop" : "start";
}

bool parse_iot_agent_control_action(std::string_view name, IotAgentControlAction* action) {
    if (action == nullptr) {
        return false;
    }
    if (name == "start") {
        *action = IotAgentControlAction::start;
        return true;
    }
    if (name == "stop") {
        *action = IotAgentControlAction::stop;
        return true;
    }
    return false;
}

std::string_view wifi_profile_action_name(WifiProfileAction action) {
    return action == WifiProfileAction::disconnect ? "disconnect" : "connect";
}

bool parse_wifi_profile_action(std::string_view name, WifiProfileAction* action) {
    if (action == nullptr) {
        return false;
    }
    if (name == "connect") {
        *action = WifiProfileAction::connect;
        return true;
    }
    if (name == "disconnect") {
        *action = WifiProfileAction::disconnect;
        return true;
    }
    return false;
}

std::string_view power_action_name(PowerAction action) {
    return action == PowerAction::shutdown ? "shutdown" : "reboot";
}

bool parse_power_action(std::string_view name, PowerAction* action) {
    if (action == nullptr) {
        return false;
    }
    if (name == "reboot") {
        *action = PowerAction::reboot;
        return true;
    }
    if (name == "shutdown") {
        *action = PowerAction::shutdown;
        return true;
    }
    return false;
}

}  // namespace micropanel_touch::core
