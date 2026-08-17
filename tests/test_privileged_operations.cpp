#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/PrivilegedOperations.h"

#include <cassert>

int main() {
    using micropanel_touch::core::StaticIpv4Operation;
    using micropanel_touch::core::StaticIpSettings;
    using micropanel_touch::core::DhcpOperation;
    using micropanel_touch::core::DhcpServerOperation;
    using micropanel_touch::core::SystemUpdateOperation;
    using micropanel_touch::core::validate_dhcp_operation;
    using micropanel_touch::core::validate_dhcp_server_operation;
    using micropanel_touch::core::validate_static_ipv4_operation;
    using micropanel_touch::core::validate_system_update_operation;

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
    const DhcpServerOperation server{
        "eth0", {"192.168.50.1", "24", "192.168.50.100", "192.168.50.200"}};
    assert(validate_dhcp_server_operation(server).valid);
    assert(!validate_dhcp_server_operation({"wlan0", server.settings}).valid);
    assert(!validate_dhcp_server_operation(
                {"eth0", {"192.168.50.1", "31", "192.168.50.2", "192.168.50.3"}})
                .valid);
    assert(validate_system_update_operation(
               {std::string{micropanel_touch::core::kSystemUpdateUsbSourcePath}}).valid);
    assert(validate_system_update_operation(
               {"/data/micropanel-touch-system/updates/release-00.15"})
               .valid);
    assert(!validate_system_update_operation({"relative-update"}).valid);
    assert(!validate_system_update_operation({"/dev/sda1"}).valid);
    assert(!validate_system_update_operation({"/dev/../mmcblk0p5"}).valid);
    assert(!validate_system_update_operation({"/etc/passwd"}).valid);
    return 0;
}
