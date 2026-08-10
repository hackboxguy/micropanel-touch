#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/PrivilegedBroker.h"

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
    std::optional<micropanel_touch::core::StaticIpv4Operation> executed;
    unsigned int execution_count = 0U;
    micropanel_touch::platform::PrivilegedBrokerServer server(
        [&executed, &execution_count](const micropanel_touch::core::StaticIpv4Operation& operation,
                                      const std::atomic_bool&) {
            ++execution_count;
            executed = operation;
            return micropanel_touch::core::PrivilegedOperationReply{
                true, "Static IPv4 configuration applied."};
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
    assert(executed->interface_name == "eth0");
    assert(executed->settings.address == "192.168.1.20");

    const auto invalid = micropanel_touch::platform::PrivilegedBrokerClient::apply_static_ipv4(
        socket_path, {"eth0;reboot", request.settings}, &diagnostic);
    assert(!invalid.ok);
    assert(execution_count == 1U);

    const std::string unknown = raw_request(socket_path, R"({"operation":"run","argv":["id"]})");
    assert(unknown.find("\"ok\":false") != std::string::npos);
    assert(unknown.find("allowed privileged operation") != std::string::npos);
    assert(execution_count == 1U);
    const std::string malformed_static = raw_request(
        socket_path,
        R"({"operation":"apply_static_ipv4","interface":"eth0","address":"invalid","prefix_length":"24","gateway":"192.168.1.1"})");
    assert(malformed_static.find("\"ok\":false") != std::string::npos);
    assert(execution_count == 1U);

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
