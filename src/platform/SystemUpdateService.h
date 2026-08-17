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

// UI-side asynchronous client for the root-owned update pipeline. It can send
// only the typed source-path request; the handler retains all slot and reboot
// authority. A transfer intentionally has no UI cancellation protocol once
// it has begun: an interrupted inactive-slot write is safe and unbootable.
class SystemUpdateService {
public:
    SystemUpdateService(core::UiEventQueue& event_queue, std::filesystem::path broker_socket_path);
    ~SystemUpdateService();
    SystemUpdateService(const SystemUpdateService&) = delete;
    SystemUpdateService& operator=(const SystemUpdateService&) = delete;

    bool start(std::uint64_t request_id, const core::SystemUpdateOperation& operation,
               std::string* diagnostic = nullptr);
    void stop();

private:
    void run(std::uint64_t request_id, core::SystemUpdateOperation operation);

    core::UiEventQueue& event_queue_;
    std::filesystem::path broker_socket_path_;
    std::atomic_bool running_{false};
    std::atomic<std::uint64_t> next_sequence_{1U};
    std::mutex mutex_;
    std::thread worker_;
};

}  // namespace micropanel_touch::platform
