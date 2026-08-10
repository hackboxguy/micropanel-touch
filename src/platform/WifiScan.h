#pragma once

#include "core/UiEventQueue.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace micropanel_touch::platform {

std::vector<core::WifiAccessPoint> parse_nmcli_wifi_list(const std::string& output);

/**
 * Runs a single read-only NetworkManager scan on a worker thread. The result
 * is delivered through UiEventQueue; the worker never calls LVGL.
 */
class WifiScanProvider {
public:
    explicit WifiScanProvider(core::UiEventQueue& event_queue);
    ~WifiScanProvider();
    WifiScanProvider(const WifiScanProvider&) = delete;
    WifiScanProvider& operator=(const WifiScanProvider&) = delete;

    void request_scan();
    void stop();

private:
    void run();

    core::UiEventQueue& event_queue_;
    std::atomic_bool running_{false};
    std::atomic_bool cancellation_requested_{false};
    std::thread worker_;
    std::uint64_t next_sequence_{1};
};

}  // namespace micropanel_touch::platform
