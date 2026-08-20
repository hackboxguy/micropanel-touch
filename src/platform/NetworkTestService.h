#pragma once

#include "core/UiEventQueue.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace micropanel_touch::platform {

// Runs one network diagnostic at a time and streams its output to the UI.
//
// Unprivileged by design: ping is capability-granted and curl needs nothing,
// so no broker is involved. The handler owns command construction; this owns
// the lifecycle - one test at a time, bounded output, cancellable, and a
// terminal result even when the child dies badly.
class NetworkTestService {
public:
    enum class Test {
        ping,
        internet,
        speed,
    };

    NetworkTestService(core::UiEventQueue& event_queue, std::filesystem::path handler_path);
    ~NetworkTestService();
    NetworkTestService(const NetworkTestService&) = delete;
    NetworkTestService& operator=(const NetworkTestService&) = delete;

    // interface_name and target reach the handler as separate arguments and are
    // never concatenated into one; the handler validates both again.
    bool start(std::uint64_t request_id, Test test, const std::string& interface_name,
               const std::string& target, std::string* diagnostic = nullptr);
    void cancel();
    void stop();

    static std::string_view test_name(Test test);

private:
    void run(std::uint64_t request_id, Test test, std::string interface_name, std::string target);

    core::UiEventQueue& event_queue_;
    std::filesystem::path handler_path_;
    std::atomic_bool running_{false};
    std::atomic_bool cancellation_requested_{false};
    std::atomic<std::uint64_t> next_sequence_{1U};
    std::mutex mutex_;
    std::thread worker_;
};

}  // namespace micropanel_touch::platform
