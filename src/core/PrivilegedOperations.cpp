#include "core/PrivilegedOperations.h"

#include <algorithm>
#include <cctype>

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

}  // namespace micropanel_touch::core
