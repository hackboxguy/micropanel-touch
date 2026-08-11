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

// UI-side asynchronous client for the one allowlisted network-write broker
// operation. A socket must be configured explicitly; creating this object or
// displaying IP Settings never starts a privileged service or changes a
// network connection.
class StaticIpv4ApplyService {
public:
    StaticIpv4ApplyService(core::UiEventQueue& event_queue,
                           std::filesystem::path broker_socket_path);
    ~StaticIpv4ApplyService();
    StaticIpv4ApplyService(const StaticIpv4ApplyService&) = delete;
    StaticIpv4ApplyService& operator=(const StaticIpv4ApplyService&) = delete;

    bool start(std::uint64_t request_id, const core::StaticIpv4Operation& operation,
               std::string* diagnostic = nullptr);
    void stop();

private:
    void run(std::uint64_t request_id, core::StaticIpv4Operation operation);

    core::UiEventQueue& event_queue_;
    std::filesystem::path broker_socket_path_;
    std::atomic_bool running_{false};
    std::atomic<std::uint64_t> next_sequence_{1U};
    std::mutex mutex_;
    std::thread worker_;
};

}  // namespace micropanel_touch::platform
