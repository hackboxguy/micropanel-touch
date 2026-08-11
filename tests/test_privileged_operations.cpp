#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/PrivilegedOperations.h"

#include <cassert>

int main() {
    using micropanel_touch::core::StaticIpv4Operation;
    using micropanel_touch::core::StaticIpSettings;
    using micropanel_touch::core::DhcpOperation;
    using micropanel_touch::core::validate_dhcp_operation;
    using micropanel_touch::core::validate_static_ipv4_operation;

    const StaticIpv4Operation valid{"eth0", {"192.168.1.20", "24", "192.168.1.1"}};
    assert(validate_static_ipv4_operation(valid).valid);
    assert(validate_static_ipv4_operation({"wlan0", valid.settings}).valid);
    assert(validate_static_ipv4_operation({"vlan.100", valid.settings}).valid);

    assert(!validate_static_ipv4_operation({"", valid.settings}).valid);
    assert(!validate_static_ipv4_operation({"interface-name-too-long", valid.settings}).valid);
    assert(!validate_static_ipv4_operation({"eth0;reboot", valid.settings}).valid);
    assert(!validate_static_ipv4_operation({"../../eth0", valid.settings}).valid);
    assert(!validate_static_ipv4_operation({"eth0", {"999.0.0.1", "24", "192.168.1.1"}}).valid);
    assert(validate_dhcp_operation({"eth0"}).valid);
    assert(!validate_dhcp_operation({"eth0;reboot"}).valid);
    return 0;
}
