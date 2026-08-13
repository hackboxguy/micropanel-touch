#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/StaticIpSettings.h"

#include <cassert>

using micropanel_touch::core::StaticIpSettings;
using micropanel_touch::core::DhcpServerSettings;
using micropanel_touch::core::validate_dhcp_server_ipv4;
using micropanel_touch::core::validate_static_ipv4;
using micropanel_touch::core::prefix_length_from_ipv4_netmask;

int main() {
    assert(validate_static_ipv4({"192.168.1.50", "24", "192.168.1.1"}).valid);
    assert(validate_static_ipv4({"10.0.0.2", "0", "10.0.0.1"}).valid);

    assert(!validate_static_ipv4({"192.168.1", "24", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.50.", "24", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.256", "24", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.50", "33", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.50", "twenty-four", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.50", "24", "192.168.1"}).valid);

    assert(prefix_length_from_ipv4_netmask("255.255.255.0").value() == "24");
    assert(prefix_length_from_ipv4_netmask("255.255.254.0").value() == "23");
    assert(prefix_length_from_ipv4_netmask("0.0.0.0").value() == "0");
    assert(!prefix_length_from_ipv4_netmask("255.0.255.0").has_value());
    assert(!prefix_length_from_ipv4_netmask("255.255.255").has_value());

    assert(validate_dhcp_server_ipv4(
               {"192.168.50.1", "24", "192.168.50.100", "192.168.50.200"})
               .valid);
    assert(!validate_dhcp_server_ipv4(
                {"192.168.50.1", "24", "192.168.51.100", "192.168.51.200"})
                .valid);
    assert(!validate_dhcp_server_ipv4(
                {"192.168.50.1", "24", "192.168.50.1", "192.168.50.200"})
                .valid);
    assert(!validate_dhcp_server_ipv4(
                {"192.168.50.1", "24", "192.168.50.200", "192.168.50.100"})
                .valid);
    assert(!validate_dhcp_server_ipv4(
                {"203.0.113.1", "24", "203.0.113.100", "203.0.113.200"})
                .valid);
    return 0;
}
