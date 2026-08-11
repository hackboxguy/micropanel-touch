#include "core/PrivilegedOperations.h"

#include <algorithm>
#include <cctype>
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

StaticIpValidationResult validate_network_operation(const NetworkOperation& operation) {
    return std::visit([](const auto& selected) {
        using Operation = std::decay_t<decltype(selected)>;
        if constexpr (std::is_same_v<Operation, StaticIpv4Operation>) {
            return validate_static_ipv4_operation(selected);
        } else {
            return validate_dhcp_operation(selected);
        }
    }, operation);
}

}  // namespace micropanel_touch::core
