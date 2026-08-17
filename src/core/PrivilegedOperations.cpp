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

bool has_parent_reference(const std::string& path) {
    return path == ".." || path.rfind("../", 0U) == 0U ||
           path.find("/../") != std::string::npos ||
           (path.size() >= 3U && path.compare(path.size() - 3U, 3U, "/..") == 0);
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
        } else {
            return validate_dhcp_server_operation(selected);
        }
    }, operation);
}

StaticIpValidationResult validate_system_update_operation(const SystemUpdateOperation& operation) {
    // USB is mounted by the root handler.  The data-directory form is an
    // intentionally narrow local-file escape hatch for bench/release tooling;
    // it is not a general filesystem read primitive exposed to the UI.
    constexpr std::string_view kLocalPrefix{"/data/micropanel-touch-system/updates/"};
    const std::string& source = operation.source_path;
    if (source.empty() || source.size() > 4096U || source.front() != '/' ||
        has_parent_reference(source)) {
        return {false, "Update source must be a safe absolute path."};
    }
    if (source != kSystemUpdateUsbSourcePath &&
        source.rfind(std::string{kLocalPrefix}, 0U) != 0U) {
        return {false, "Update source is outside the allowed USB or local update paths."};
    }
    return {true, "System update source is valid; no update has been applied."};
}

}  // namespace micropanel_touch::core
