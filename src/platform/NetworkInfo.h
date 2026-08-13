#pragma once

#include "core/UiEventQueue.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace micropanel_touch::platform {

// Decode the one-field, terse, escaped `nmcli connection show` listing. The
// profile lookup below intentionally does not rely on a device being active.
std::vector<std::string> parse_nmcli_connection_names(const std::string& output);

class NetworkInfoProvider {
public:
    NetworkInfoProvider(core::UiEventQueue& event_queue, std::string managed_interface);
    ~NetworkInfoProvider();
    NetworkInfoProvider(const NetworkInfoProvider&) = delete;
    NetworkInfoProvider& operator=(const NetworkInfoProvider&) = delete;

    void start();
    void stop();
    // Profile reads start only on explicit UI demand, rather than spawning
    // NetworkManager commands alongside the frequent link-state snapshots.
    void request_managed_ipv4_profile();

    static core::NetworkSnapshot collect_snapshot();

private:
    void run();

    core::UiEventQueue& event_queue_;
    std::atomic_bool running_{false};
    std::atomic_bool cancellation_requested_{false};
    std::atomic_bool profile_refresh_requested_{false};
    std::string managed_interface_;
    std::condition_variable wake_condition_;
    std::mutex wake_mutex_;
    std::thread worker_;
    std::uint64_t next_sequence_{1};
};

}  // namespace micropanel_touch::platform
