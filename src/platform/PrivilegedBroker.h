#pragma once

#include "core/PrivilegedOperations.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include <sys/types.h>

namespace micropanel_touch::platform {

// The broker sends its only reply after the package-owned handler exits. Keep
// this ceiling and the client reply timeout together so a valid NetworkManager
// operation cannot be reported as a client-side timeout while it is still
// applying. The extra margin covers scheduling and final broker serialization.
inline constexpr auto kNetworkOperationTimeout = std::chrono::seconds(45);
inline constexpr auto kBrokerClientReplyTimeout = std::chrono::seconds(60);
static_assert(kBrokerClientReplyTimeout > kNetworkOperationTimeout);

// The executor is selected by the root-owned broker process, never by a
// client. It receives one of the broker's typed allowlisted requests and a
// cancellation signal only.
using NetworkExecutor = std::function<core::PrivilegedOperationReply(
    const core::NetworkOperation&, const std::atomic_bool& cancellation_requested)>;

// Root-side local broker. It accepts one small JSON request per AF_UNIX
// connection, authenticates the peer with SO_PEERCRED, and exposes only
// apply_static_ipv4, apply_dhcp, and apply_dhcp_server. It never accepts an
// executable, argv, or shell expression from the client.
class PrivilegedBrokerServer {
public:
    explicit PrivilegedBrokerServer(NetworkExecutor network_executor);
    ~PrivilegedBrokerServer();
    PrivilegedBrokerServer(const PrivilegedBrokerServer&) = delete;
    PrivilegedBrokerServer& operator=(const PrivilegedBrokerServer&) = delete;

    bool start(const std::filesystem::path& socket_path, uid_t allowed_uid,
               std::string* diagnostic = nullptr);
    void stop();

private:
    void serve();

    NetworkExecutor network_executor_;
    std::atomic_bool running_{false};
    // This is deliberately separate from running_: CommandRunner interprets
    // true as a request to terminate the selected handler.
    std::atomic_bool cancellation_requested_{false};
    std::atomic_int listen_fd_{-1};
    std::atomic_int active_client_fd_{-1};
    std::filesystem::path socket_path_;
    uid_t allowed_uid_{static_cast<uid_t>(-1)};
    std::thread worker_;
};

class PrivilegedBrokerClient {
public:
    static core::PrivilegedOperationReply apply_static_ipv4(
        const std::filesystem::path& socket_path, const core::StaticIpv4Operation& operation,
        std::string* diagnostic = nullptr);
    static core::PrivilegedOperationReply apply_dhcp(
        const std::filesystem::path& socket_path, const core::DhcpOperation& operation,
        std::string* diagnostic = nullptr);
    static core::PrivilegedOperationReply apply_dhcp_server(
        const std::filesystem::path& socket_path, const core::DhcpServerOperation& operation,
        std::string* diagnostic = nullptr);
};

}  // namespace micropanel_touch::platform
