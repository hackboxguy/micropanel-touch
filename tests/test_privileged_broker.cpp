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

    // Factory reset carries nothing: the bare operation name is the whole
    // request, so there is no field for a client to influence.
    const auto reset = micropanel_touch::platform::PrivilegedBrokerClient::factory_reset(
        socket_path, &diagnostic);
    assert(reset.ok);
    assert(execution_count == 5U);
    assert(std::holds_alternative<micropanel_touch::core::FactoryResetOperation>(*executed));

    const auto slow_dhcp = micropanel_touch::platform::PrivilegedBrokerClient::apply_dhcp(
        socket_path, {"slow0"}, &diagnostic);
    assert(slow_dhcp.ok);
    assert(slow_dhcp.message == "DHCP configuration applied.");
    assert(execution_count == 6U);

    const auto invalid = micropanel_touch::platform::PrivilegedBrokerClient::apply_static_ipv4(
        socket_path, {"eth0;reboot", request.settings}, &diagnostic);
    assert(!invalid.ok);
    const auto invalid_update = micropanel_touch::platform::PrivilegedBrokerClient::apply_system_update(
        socket_path, {"/dev/disk/by-label/MP_UPDATE"}, &diagnostic);
    assert(!invalid_update.ok);
    assert(execution_count == 6U);

    const std::string unknown = raw_request(socket_path, R"({"operation":"run","argv":["id"]})");
    assert(unknown.find("\"ok\":false") != std::string::npos);
    assert(unknown.find("allowed privileged operation") != std::string::npos);
    assert(execution_count == 6U);
    const std::string malformed_static = raw_request(
        socket_path,
        R"({"operation":"apply_static_ipv4","interface":"eth0","address":"invalid","prefix_length":"24","gateway":"192.168.1.1"})");
    assert(malformed_static.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 6U);
    const std::string malformed_dhcp = raw_request(
        socket_path, R"({"operation":"apply_dhcp","interface":"eth0","address":"unexpected"})");
    assert(malformed_dhcp.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 6U);
    const std::string malformed_update = raw_request(
        socket_path, R"({"operation":"apply_system_update","source":"usb","extra":true})");
    assert(malformed_update.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 6U);
    // The retired path form must not survive as an accepted wire field.
    const std::string legacy_update = raw_request(
        socket_path,
        R"({"operation":"apply_system_update","source_path":"/dev/disk/by-label/MP_UPDATE"})");
    assert(legacy_update.find("\"ok\":false") != std::string::npos);
    // A factory-reset request with anything else in it is not a factory-reset
    // request. Accepting extra fields would be the start of a client-supplied
    // target for the one operation that erases the device.
    const std::string malformed_reset = raw_request(
        socket_path, R"({"operation":"factory_reset","scope":"everything"})");
    assert(malformed_reset.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 6U);
    const std::string malformed_dhcp_server = raw_request(
        socket_path,
        R"({"operation":"apply_dhcp_server","interface":"eth0","address":"192.168.50.1","prefix_length":"24","lease_start":"192.168.50.100"})");
    assert(malformed_dhcp_server.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 6U);

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
