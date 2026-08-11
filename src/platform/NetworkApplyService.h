#pragma once

#include "core/PrivilegedOperations.h"
#include "core/UiEventQueue.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace micropanel_touch::platform {

// UI-side asynchronous client for the small allowlisted network-write broker
// surface. A socket must be configured explicitly; creating this object or
// displaying IP Settings never starts a privileged service or changes a
// network connection.
class NetworkApplyService {
public:
    NetworkApplyService(core::UiEventQueue& event_queue,
                        std::filesystem::path broker_socket_path);
    ~NetworkApplyService();
    NetworkApplyService(const NetworkApplyService&) = delete;
    NetworkApplyService& operator=(const NetworkApplyService&) = delete;

    bool start(std::uint64_t request_id, const core::NetworkOperation& operation,
               std::string* diagnostic = nullptr);
    void stop();

private:
    void run(std::uint64_t request_id, core::NetworkOperation operation);

    core::UiEventQueue& event_queue_;
    std::filesystem::path broker_socket_path_;
    std::atomic_bool running_{false};
    std::atomic<std::uint64_t> next_sequence_{1U};
    std::mutex mutex_;
    std::thread worker_;
};

}  // namespace micropanel_touch::platform
