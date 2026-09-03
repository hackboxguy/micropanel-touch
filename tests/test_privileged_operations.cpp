#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/PrivilegedOperations.h"

#include <cassert>
#include <string>

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

    // The IoT agent account: a bare JID, an optional host, a password with no
    // whitespace (the agent's login parser splits on it) and no control
    // characters (each would become a second line of the file), plus the
    // optional port, BOSH and admin values.
    using micropanel_touch::core::IotAgentConfigOperation;
    using micropanel_touch::core::validate_iot_agent_config_operation;
    const auto agent = [](std::string user, std::string server, std::string password) {
        IotAgentConfigOperation operation;
        operation.user = std::move(user);
        operation.server = std::move(server);
        operation.password = std::move(password);
        return operation;
    };
    const auto agent_ok = [](const IotAgentConfigOperation& operation) {
        return validate_iot_agent_config_operation(operation).valid;
    };
    assert(agent_ok(agent("bot@example.org", "", "s3cret-Pa55")));
    assert(agent_ok(agent("bot@example.org", "xmpp.example.org", "s3cret-Pa55")));
    assert(agent_ok(agent("my.bot-1@sub.example-host.org", "host", "p")));
    assert(!agent_ok(agent("", "", "pw")));
    assert(!agent_ok(agent("bot", "", "pw")));
    assert(!agent_ok(agent("@example.org", "", "pw")));
    assert(!agent_ok(agent("bot@", "", "pw")));
    assert(!agent_ok(agent("bot@example.org/panel", "", "pw")));
    assert(!agent_ok(agent("a@b@example.org", "", "pw")));
    assert(!agent_ok(agent("bot@exa mple.org", "", "pw")));
    assert(!agent_ok(agent("bot@example.org\nadminbuddy: x@y", "", "pw")));
    assert(!agent_ok(agent("bot@example.org", "xmpp example.org", "pw")));
    assert(!agent_ok(agent("bot@example.org", "xmpp.example.org:5222", "pw")));
    assert(!agent_ok(agent("bot@example.org", "-bad.org", "pw")));
    assert(!agent_ok(agent("bot@example.org", "", "")));
    assert(!agent_ok(agent("bot@example.org", "", "two words")));
    assert(!agent_ok(agent("bot@example.org", "", "tab\there")));
    assert(!agent_ok(agent("bot@example.org", "", "line\nbreak")));
    assert(!agent_ok(agent("bot@example.org", "", std::string(129U, 'x'))));
    assert(agent_ok(agent("bot@example.org", "", std::string(128U, 'x'))));
    {
        IotAgentConfigOperation operation = agent("bot@example.org", "", "pw");
        operation.port = 5222U;
        assert(agent_ok(operation));
        operation.port = 65535U;
        assert(agent_ok(operation));
        operation.port = 65536U;
        assert(!agent_ok(operation));
        operation.port = 0U;
        operation.admin = "owner@example.org";
        assert(agent_ok(operation));
        operation.admin = "owner";
        assert(!agent_ok(operation));
        operation.admin = "owner@example.org/phone";
        assert(!agent_ok(operation));
        operation.admin.clear();
        // BOSH needs a URL; a URL is fine on its own (kept for later).
        operation.bosh = true;
        assert(!agent_ok(operation));
        operation.bosh_url = "https://xmpp.example.org:5281/http-bind";
        assert(agent_ok(operation));
        operation.bosh_url = "http://xmpp.example.org/http-bind";
        assert(agent_ok(operation));
        operation.bosh_url = "https://xmpp.example.org";
        assert(agent_ok(operation));
        operation.bosh_url = "ftp://xmpp.example.org/http-bind";
        assert(!agent_ok(operation));
        operation.bosh_url = "xmpp.example.org/http-bind";
        assert(!agent_ok(operation));
        operation.bosh_url = "https://xmpp.example.org:99999/http-bind";
        assert(!agent_ok(operation));
        operation.bosh_url = "https://xmpp.example.org:0/http-bind";
        assert(!agent_ok(operation));
        operation.bosh_url = "https://xmpp example.org/http-bind";
        assert(!agent_ok(operation));
        operation.bosh_url = "https://xmpp.example.org/http-bind\nbosh: false";
        assert(!agent_ok(operation));
        operation.bosh_url = "https://xmpp.example.org:5281/http-bind";
        operation.bosh_host = "example.org";
        assert(agent_ok(operation));
        operation.bosh_host = "example org";
        assert(!agent_ok(operation));
        operation.bosh_host.clear();
        operation.bosh = false;
        assert(agent_ok(operation));  // URL kept while BOSH is off
    }
    // No diagnostic ever quotes the password.
    for (const IotAgentConfigOperation& rejected :
         {agent("bot@example.org", "", "two words"),
          agent("bot@example.org", "", std::string(129U, 'z')),
          agent("bot", "", "leaky-secret")}) {
        const auto result = validate_iot_agent_config_operation(rejected);
        assert(!result.valid);
        assert(result.message.find(rejected.password) == std::string::npos);
        assert(result.message.find("129") == std::string::npos);
    }
    // The control action vocabulary is two words, spelled exactly.
    using micropanel_touch::core::IotAgentControlAction;
    using micropanel_touch::core::parse_iot_agent_control_action;
    IotAgentControlAction control_action{};
    assert(parse_iot_agent_control_action("start", &control_action) &&
           control_action == IotAgentControlAction::start);
    assert(parse_iot_agent_control_action("stop", &control_action) &&
           control_action == IotAgentControlAction::stop);
    assert(!parse_iot_agent_control_action("restart", &control_action));
    assert(!parse_iot_agent_control_action("Stop", &control_action));
    assert(!parse_iot_agent_control_action("", &control_action));
    assert(!parse_iot_agent_control_action("stop", nullptr));
    assert(micropanel_touch::core::iot_agent_control_action_name(IotAgentControlAction::stop) ==
           "stop");
    return 0;
}
