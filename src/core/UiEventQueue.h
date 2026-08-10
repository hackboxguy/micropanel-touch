#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace micropanel_touch::core {

struct NetworkInterfaceStatus {
    std::string name;
    std::string mac_address;
    std::string link_state;
    bool carrier{false};
    std::vector<std::string> ipv4_addresses;
};

struct NetworkSnapshot {
    std::vector<NetworkInterfaceStatus> interfaces;
};

struct WifiAccessPoint {
    bool active{false};
    std::string ssid;
    std::string bssid;
    unsigned int signal_percent{0};
    std::string security;
};

struct WifiScanResult {
    std::vector<WifiAccessPoint> access_points;
    std::string diagnostic;
};

using UiEventPayload = std::variant<NetworkSnapshot, WifiScanResult>;

struct UiEvent {
    std::uint64_t sequence{0};
    UiEventPayload payload;
};

/**
 * The only route from worker threads into the UI. Events own all their data
 * and are drained by an LVGL timer on the UI thread. Use push() for ordered
 * events (future action results and log lines) and push_latest() only for
 * replaceable state snapshots.
 */
class UiEventQueue {
public:
    void push(UiEvent event);
    void push_latest(UiEvent event);
    std::vector<UiEvent> drain();

private:
    std::mutex mutex_;
    std::deque<UiEvent> events_;
};

}  // namespace micropanel_touch::core
