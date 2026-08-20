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
