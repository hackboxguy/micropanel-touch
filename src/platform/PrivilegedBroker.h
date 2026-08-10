#pragma once

#include "core/PrivilegedOperations.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include <sys/types.h>

namespace micropanel_touch::platform {

// The executor is selected by the root-owned broker process, never by a
// client. It receives typed data and a cancellation signal only.
using StaticIpv4Executor = std::function<core::PrivilegedOperationReply(
    const core::StaticIpv4Operation&, const std::atomic_bool& cancellation_requested)>;

// Root-side local broker. It accepts one small JSON request per AF_UNIX
// connection, authenticates the peer with SO_PEERCRED, and exposes exactly
// one operation: apply_static_ipv4. It never accepts an executable, argv, or
// shell expression from the client.
class PrivilegedBrokerServer {
public:
    explicit PrivilegedBrokerServer(StaticIpv4Executor static_ipv4_executor);
    ~PrivilegedBrokerServer();
    PrivilegedBrokerServer(const PrivilegedBrokerServer&) = delete;
    PrivilegedBrokerServer& operator=(const PrivilegedBrokerServer&) = delete;

    bool start(const std::filesystem::path& socket_path, uid_t allowed_uid,
               std::string* diagnostic = nullptr);
    void stop();

private:
    void serve();

    StaticIpv4Executor static_ipv4_executor_;
    std::atomic_bool running_{false};
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
};

}  // namespace micropanel_touch::platform
