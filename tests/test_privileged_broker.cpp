#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/PrivilegedBroker.h"
#include "core/PrivilegedOperations.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::string raw_request(const std::filesystem::path& socket_path, const std::string& request) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1U);
    assert(connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    const std::string wire = request + "\n";
    assert(send(fd, wire.data(), wire.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(wire.size()));
    std::string response;
    char buffer[256];
    while (response.find('\n') == std::string::npos) {
        const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        assert(count > 0);
        response.append(buffer, static_cast<std::size_t>(count));
    }
    close(fd);
    return response;
}

}  // namespace

int main() {
    const auto socket_path = std::filesystem::temp_directory_path() /
                             ("micropanel-touch-broker-" + std::to_string(getpid()) + ".sock");
    std::optional<micropanel_touch::core::PrivilegedOperation> executed;
    unsigned int execution_count = 0U;
    micropanel_touch::platform::PrivilegedBrokerServer server(
        [&executed, &execution_count](const micropanel_touch::core::PrivilegedOperation& operation,
                                      const std::atomic_bool& cancellation_requested) {
            assert(!cancellation_requested.load());
            ++execution_count;
            executed = operation;
            if (const auto* const dhcp =
                    std::get_if<micropanel_touch::core::DhcpOperation>(&operation);
                dhcp != nullptr && dhcp->interface_name == "slow0") {
                // The client waits for the broker's terminal result, not just
                // a quick accept. This exceeds the historical five-second
                // receive timeout and protects against regressions that would
                // falsely report a valid NetworkManager operation as failed.
                std::this_thread::sleep_for(std::chrono::seconds(6));
            }
            return micropanel_touch::core::PrivilegedOperationReply{
                true, std::holds_alternative<micropanel_touch::core::StaticIpv4Operation>(operation)
                          ? "Static IPv4 configuration applied."
                          : (std::holds_alternative<micropanel_touch::core::DhcpServerOperation>(operation)
                                 ? "DHCP server configuration applied."
                                 : (std::holds_alternative<micropanel_touch::core::SystemUpdateOperation>(operation)
                                        ? "System update verified; rebooting into the candidate slot."
                                        : "DHCP configuration applied."))};
        });
    std::string diagnostic;
    assert(server.start(socket_path, getuid(), &diagnostic));

    struct stat metadata {};
    assert(stat(socket_path.c_str(), &metadata) == 0);
    assert(metadata.st_uid == getuid());
    assert((metadata.st_mode & 0777) == 0600);

    const micropanel_touch::core::StaticIpv4Operation request{
        "eth0", {"192.168.1.20", "24", "192.168.1.1"}};
    const auto applied = micropanel_touch::platform::PrivilegedBrokerClient::apply_static_ipv4(
        socket_path, request, &diagnostic);
    assert(applied.ok);
    assert(applied.message == "Static IPv4 configuration applied.");
    assert(executed.has_value());
    assert(execution_count == 1U);
    const auto* executed_static = std::get_if<micropanel_touch::core::StaticIpv4Operation>(&*executed);
    assert(executed_static != nullptr);
    assert(executed_static->interface_name == "eth0");
    assert(executed_static->settings.address == "192.168.1.20");

    const auto dhcp = micropanel_touch::platform::PrivilegedBrokerClient::apply_dhcp(
        socket_path, {"eth0"}, &diagnostic);
    assert(dhcp.ok);
    assert(dhcp.message == "DHCP configuration applied.");
    assert(execution_count == 2U);
    const auto* executed_dhcp = std::get_if<micropanel_touch::core::DhcpOperation>(&*executed);
    assert(executed_dhcp != nullptr);
    assert(executed_dhcp->interface_name == "eth0");

    const micropanel_touch::core::DhcpServerOperation server_request{
        "eth0", {"192.168.50.1", "24", "192.168.50.100", "192.168.50.200"}};
    const auto server_reply = micropanel_touch::platform::PrivilegedBrokerClient::apply_dhcp_server(
        socket_path, server_request, &diagnostic);
    assert(server_reply.ok);
    assert(server_reply.message == "DHCP server configuration applied.");
    assert(execution_count == 3U);
    const auto* executed_server =
        std::get_if<micropanel_touch::core::DhcpServerOperation>(&*executed);
    assert(executed_server != nullptr);
    assert(executed_server->settings.lease_start == "192.168.50.100");

    const auto update = micropanel_touch::platform::PrivilegedBrokerClient::apply_system_update(
        socket_path, {std::string{micropanel_touch::core::kSystemUpdateUsbSource}}, &diagnostic);
    assert(update.ok);
    assert(update.message == "System update verified; rebooting into the candidate slot.");
    assert(execution_count == 4U);
    const auto* executed_update =
        std::get_if<micropanel_touch::core::SystemUpdateOperation>(&*executed);
    assert(executed_update != nullptr);
    assert(executed_update->source == micropanel_touch::core::kSystemUpdateUsbSource);

    const auto ota_update = micropanel_touch::platform::PrivilegedBrokerClient::apply_system_update(
        socket_path, {std::string{micropanel_touch::core::kSystemUpdateOtaSource}}, &diagnostic);
    assert(ota_update.ok);
    assert(execution_count == 5U);
    const auto* executed_ota =
        std::get_if<micropanel_touch::core::SystemUpdateOperation>(&*executed);
    assert(executed_ota != nullptr);
    assert(executed_ota->source == micropanel_touch::core::kSystemUpdateOtaSource);

    // Asking what the server offers carries nothing either - which server, and
    // which key to trust, are both pinned in the image.
    const auto check = micropanel_touch::platform::PrivilegedBrokerClient::check_system_update(
        socket_path, &diagnostic);
    assert(check.ok);
    assert(execution_count == 6U);
    assert(std::holds_alternative<micropanel_touch::core::CheckSystemUpdateOperation>(*executed));

    // Factory reset carries nothing: the bare operation name is the whole
    // request, so there is no field for a client to influence.
    const auto reset = micropanel_touch::platform::PrivilegedBrokerClient::factory_reset(
        socket_path, &diagnostic);
    assert(reset.ok);
    assert(execution_count == 7U);
    assert(std::holds_alternative<micropanel_touch::core::FactoryResetOperation>(*executed));

    // Power carries an enum and nothing else. Both actions cross the wire as
    // their own word, so a client cannot ask for a target that is not one of
    // the two the handler knows.
    const auto reboot = micropanel_touch::platform::PrivilegedBrokerClient::power(
        socket_path, {micropanel_touch::core::PowerAction::reboot}, &diagnostic);
    assert(reboot.ok);
    assert(execution_count == 8U);
    const auto* executed_reboot = std::get_if<micropanel_touch::core::PowerOperation>(&*executed);
    assert(executed_reboot != nullptr);
    assert(executed_reboot->action == micropanel_touch::core::PowerAction::reboot);

    const auto shutdown = micropanel_touch::platform::PrivilegedBrokerClient::power(
        socket_path, {micropanel_touch::core::PowerAction::shutdown}, &diagnostic);
    assert(shutdown.ok);
    assert(execution_count == 9U);
    const auto* executed_shutdown = std::get_if<micropanel_touch::core::PowerOperation>(&*executed);
    assert(executed_shutdown != nullptr);
    assert(executed_shutdown->action == micropanel_touch::core::PowerAction::shutdown);

    // Wi-Fi join is the one request that carries a secret. It crosses the wire
    // whole, and nothing else about the exchange repeats it.
    const auto join = micropanel_touch::platform::PrivilegedBrokerClient::wifi_join(
        socket_path, {"Bench AP", "correct-horse-battery"}, &diagnostic);
    assert(join.ok);
    assert(execution_count == 10U);
    const auto* executed_join = std::get_if<micropanel_touch::core::WifiJoinOperation>(&*executed);
    assert(executed_join != nullptr);
    assert(executed_join->ssid == "Bench AP");
    assert(executed_join->passphrase == "correct-horse-battery");
    // The reply names the network, never the password.
    assert(join.message.find("correct-horse-battery") == std::string::npos);
    assert(diagnostic.find("correct-horse-battery") == std::string::npos);

    // An open network is the same operation with an absent field, not a
    // different one - one code path to keep honest about redaction.
    const auto open_join = micropanel_touch::platform::PrivilegedBrokerClient::wifi_join(
        socket_path, {"Open AP", ""}, &diagnostic);
    assert(open_join.ok);
    assert(execution_count == 11U);
    const auto* executed_open = std::get_if<micropanel_touch::core::WifiJoinOperation>(&*executed);
    assert(executed_open != nullptr);
    assert(executed_open->passphrase.empty());

    // Connect and disconnect carry an enum and no credential: they act on the
    // profile the device already saved.
    const auto reconnect = micropanel_touch::platform::PrivilegedBrokerClient::wifi_profile(
        socket_path, {micropanel_touch::core::WifiProfileAction::connect}, &diagnostic);
    assert(reconnect.ok);
    {
        const auto* profile =
            std::get_if<micropanel_touch::core::WifiProfileOperation>(&*executed);
        assert(profile != nullptr);
        assert(profile->action == micropanel_touch::core::WifiProfileAction::connect);
    }
    const auto disconnect = micropanel_touch::platform::PrivilegedBrokerClient::wifi_profile(
        socket_path, {micropanel_touch::core::WifiProfileAction::disconnect}, &diagnostic);
    assert(disconnect.ok);
    {
        const auto* profile =
            std::get_if<micropanel_touch::core::WifiProfileOperation>(&*executed);
        assert(profile != nullptr);
        assert(profile->action == micropanel_touch::core::WifiProfileAction::disconnect);
    }

    const auto forget = micropanel_touch::platform::PrivilegedBrokerClient::wifi_forget(
        socket_path, &diagnostic);
    assert(forget.ok);
    assert(execution_count == 14U);
    assert(std::holds_alternative<micropanel_touch::core::WifiForgetOperation>(*executed));

    // A password too short to be a WPA key is refused before it leaves the
    // process, and the refusal does not quote it back.
    const auto short_password = micropanel_touch::platform::PrivilegedBrokerClient::wifi_join(
        socket_path, {"Bench AP", "short"}, &diagnostic);
    assert(!short_password.ok);
    assert(short_password.message.find("short") == std::string::npos ||
           short_password.message == "A Wi-Fi password must be at least 8 characters.");
    assert(execution_count == 14U);

    const auto slow_dhcp = micropanel_touch::platform::PrivilegedBrokerClient::apply_dhcp(
        socket_path, {"slow0"}, &diagnostic);
    assert(slow_dhcp.ok);
    assert(slow_dhcp.message == "DHCP configuration applied.");
    assert(execution_count == 15U);


    const auto invalid = micropanel_touch::platform::PrivilegedBrokerClient::apply_static_ipv4(
        socket_path, {"eth0;reboot", request.settings}, &diagnostic);
    assert(!invalid.ok);
    const auto invalid_update = micropanel_touch::platform::PrivilegedBrokerClient::apply_system_update(
        socket_path, {"/dev/disk/by-label/MP_UPDATE"}, &diagnostic);
    assert(!invalid_update.ok);
    assert(execution_count == 15U);

    const std::string unknown = raw_request(socket_path, R"({"operation":"run","argv":["id"]})");
    assert(unknown.find("\"ok\":false") != std::string::npos);
    assert(unknown.find("allowed privileged operation") != std::string::npos);
    assert(execution_count == 15U);
    const std::string malformed_static = raw_request(
        socket_path,
        R"({"operation":"apply_static_ipv4","interface":"eth0","address":"invalid","prefix_length":"24","gateway":"192.168.1.1"})");
    assert(malformed_static.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 15U);
    const std::string malformed_dhcp = raw_request(
        socket_path, R"({"operation":"apply_dhcp","interface":"eth0","address":"unexpected"})");
    assert(malformed_dhcp.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 15U);
    const std::string malformed_update = raw_request(
        socket_path, R"({"operation":"apply_system_update","source":"usb","extra":true})");
    assert(malformed_update.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 15U);
    // The retired path form must not survive as an accepted wire field.
    const std::string legacy_update = raw_request(
        socket_path,
        R"({"operation":"apply_system_update","source_path":"/dev/disk/by-label/MP_UPDATE"})");
    assert(legacy_update.find("\"ok\":false") != std::string::npos);
    // A factory-reset request with anything else in it is not a factory-reset
    // request. Accepting extra fields would be the start of a client-supplied
    // target for the one operation that erases the device.
    const std::string malformed_check = raw_request(
        socket_path, R"({"operation":"check_system_update","source":"ota"})");
    assert(malformed_check.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 15U);
    const std::string malformed_reset = raw_request(
        socket_path, R"({"operation":"factory_reset","scope":"everything"})");
    assert(malformed_reset.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 15U);
    // An action the enum does not contain is refused at the wire, before any
    // handler is chosen - "poweroff" and "halt" are real systemd verbs, and
    // neither is in this vocabulary.
    // A bare ssid is *not* here: that is a valid open-network join. These are
    // the shapes that must not reach a handler.
    for (const char* rejected : {R"({"operation":"wifi_join","ssid":"AP","passphrase":12345678})",
                                 R"({"operation":"wifi_join","ssid":"","passphrase":"12345678"})",
                                 R"({"operation":"wifi_join","ssid":"AP","passphrase":"1234567"})",
                                 R"({"operation":"wifi_join","ssid":"AP","passphrase":"12345678","autoconnect":true})",
                                 R"({"operation":"wifi_forget","ssid":"AP"})",
                                 R"({"operation":"wifi_profile","action":"up"})",
                                 R"({"operation":"wifi_profile"})",
                                 R"({"operation":"wifi_profile","action":"connect","ssid":"AP"})"}) {
        const std::string refused = raw_request(socket_path, rejected);
        assert(refused.find("\"ok\":false") != std::string::npos);
        assert(execution_count == 15U);
    }
    for (const char* rejected : {R"({"operation":"power","action":"poweroff"})",
                                 R"({"operation":"power","action":"halt"})",
                                 R"({"operation":"power","action":"reboot -f"})",
                                 R"({"operation":"power"})",
                                 R"({"operation":"power","action":"reboot","delay":0})"}) {
        const std::string refused = raw_request(socket_path, rejected);
        assert(refused.find("\"ok\":false") != std::string::npos);
        assert(execution_count == 15U);
    }
    const std::string malformed_dhcp_server = raw_request(
        socket_path,
        R"({"operation":"apply_dhcp_server","interface":"eth0","address":"192.168.50.1","prefix_length":"24","lease_start":"192.168.50.100"})");
    assert(malformed_dhcp_server.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 15U);

    // The IoT agent account is the other request that carries a secret. It
    // crosses the wire whole and nothing about the exchange repeats it.
    micropanel_touch::core::IotAgentConfigOperation agent_operation;
    agent_operation.user = "bot@example.org";
    agent_operation.server = "xmpp.example.org";
    agent_operation.password = "s3cret-Pa55";
    agent_operation.port = 5223U;
    agent_operation.bosh = true;
    agent_operation.bosh_url = "https://xmpp.example.org:5281/http-bind";
    agent_operation.bosh_host = "example.org";
    agent_operation.admin = "owner@example.org";
    const auto agent = micropanel_touch::platform::PrivilegedBrokerClient::iot_agent_config(
        socket_path, agent_operation, &diagnostic);
    assert(agent.ok);
    assert(execution_count == 16U);
    {
        const auto* executed_agent =
            std::get_if<micropanel_touch::core::IotAgentConfigOperation>(&*executed);
        assert(executed_agent != nullptr);
        assert(executed_agent->user == "bot@example.org");
        assert(executed_agent->server == "xmpp.example.org");
        assert(executed_agent->password == "s3cret-Pa55");
        assert(executed_agent->port == 5223U);
        assert(executed_agent->bosh);
        assert(executed_agent->bosh_url == "https://xmpp.example.org:5281/http-bind");
        assert(executed_agent->bosh_host == "example.org");
        assert(executed_agent->admin == "owner@example.org");
    }
    assert(agent.message.find("s3cret-Pa55") == std::string::npos);
    assert(diagnostic.find("s3cret-Pa55") == std::string::npos);
    // The optional fields arrive as absent fields, not empty ones.
    micropanel_touch::core::IotAgentConfigOperation minimal_operation;
    minimal_operation.user = "bot@example.org";
    minimal_operation.password = "s3cret-Pa55";
    const auto agent_default =
        micropanel_touch::platform::PrivilegedBrokerClient::iot_agent_config(
            socket_path, minimal_operation, &diagnostic);
    assert(agent_default.ok);
    assert(execution_count == 17U);
    {
        const auto* executed_agent =
            std::get_if<micropanel_touch::core::IotAgentConfigOperation>(&*executed);
        assert(executed_agent != nullptr);
        assert(executed_agent->server.empty());
        assert(executed_agent->port == 0U);
        assert(!executed_agent->bosh);
        assert(executed_agent->bosh_url.empty());
        assert(executed_agent->admin.empty());
    }
    // Refused before it leaves the process, without quoting the secret.
    micropanel_touch::core::IotAgentConfigOperation bad_operation;
    bad_operation.user = "not-a-jid";
    bad_operation.password = "two words";
    const auto agent_bad = micropanel_touch::platform::PrivilegedBrokerClient::iot_agent_config(
        socket_path, bad_operation, &diagnostic);
    assert(!agent_bad.ok);
    assert(agent_bad.message.find("two words") == std::string::npos);
    assert(execution_count == 17U);
    // Start and stop carry an enum and no credential.
    const auto agent_stop = micropanel_touch::platform::PrivilegedBrokerClient::iot_agent_control(
        socket_path, {micropanel_touch::core::IotAgentControlAction::stop}, &diagnostic);
    assert(agent_stop.ok);
    assert(execution_count == 18U);
    {
        const auto* control =
            std::get_if<micropanel_touch::core::IotAgentControlOperation>(&*executed);
        assert(control != nullptr);
        assert(control->action == micropanel_touch::core::IotAgentControlAction::stop);
    }
    const auto agent_start = micropanel_touch::platform::PrivilegedBrokerClient::iot_agent_control(
        socket_path, {micropanel_touch::core::IotAgentControlAction::start}, &diagnostic);
    assert(agent_start.ok);
    assert(execution_count == 19U);
    for (const char* rejected :
         {R"({"operation":"iot_agent_config","user":"bot@example.org"})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":""})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":12345678})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","server":7})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"a b"})",
          R"({"operation":"iot_agent_config","user":"bot@example.org\nadminbuddy: x@y","password":"pw"})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","server":"h o s t"})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","port":"5222"})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","port":70000})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","port":-1})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","bosh":"yes"})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","bosh":true})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","bosh":true,"bosh_url":"ftp://x/y"})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","admin":"owner"})",
          R"({"operation":"iot_agent_config","user":"bot@example.org","password":"pw","tls":false})",
          R"({"operation":"iot_agent_control","action":"restart"})",
          R"({"operation":"iot_agent_control"})",
          R"({"operation":"iot_agent_control","action":"stop","unit":"ssh"})"}) {
        const std::string refused = raw_request(socket_path, rejected);
        assert(refused.find("\"ok\":false") != std::string::npos);
        assert(refused.find("\"pw\"") == std::string::npos);
        assert(execution_count == 19U);
    }
    const int idle_client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(idle_client >= 0);
    sockaddr_un idle_address{};
    idle_address.sun_family = AF_UNIX;
    std::strncpy(idle_address.sun_path, socket_path.c_str(), sizeof(idle_address.sun_path) - 1U);
    assert(connect(idle_client, reinterpret_cast<const sockaddr*>(&idle_address), sizeof(idle_address)) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto stopped = std::async(std::launch::async, [&server] { server.stop(); });
    assert(stopped.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
    stopped.get();
    close(idle_client);
    assert(!std::filesystem::exists(socket_path));
    return 0;
}
