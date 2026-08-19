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
    // Stage 2b: the update source is a closed enum, never a client path.
    assert(validate_system_update_operation(
               {std::string{micropanel_touch::core::kSystemUpdateUsbSource}}).valid);
    assert(!validate_system_update_operation({""}).valid);
    assert(!validate_system_update_operation({"USB"}).valid);
    // Stage 4 adds exactly one more enum value. It names a source kind, not a
    // location: the release URL is pinned in the image, so widening the enum
    // gives a client nothing new to point anywhere.
    assert(validate_system_update_operation(
               {std::string{micropanel_touch::core::kSystemUpdateOtaSource}}).valid);
    assert(!validate_system_update_operation({"OTA"}).valid);
    assert(!validate_system_update_operation({"ota "}).valid);
    assert(!validate_system_update_operation({"stdin"}).valid);
    assert(!validate_system_update_operation({"github"}).valid);
    assert(!validate_system_update_operation({"https://example.invalid/release.mpupdate"}).valid);
    assert(!validate_system_update_operation({"/dev/disk/by-label/MP_UPDATE"}).valid);
    assert(!validate_system_update_operation(
               {"/data/micropanel-touch-system/updates/release-00.15"})
               .valid);
    assert(!validate_system_update_operation({"/etc/passwd"}).valid);
    return 0;
}
