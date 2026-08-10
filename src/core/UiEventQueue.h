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

using UiEventPayload = std::variant<NetworkSnapshot>;

struct UiEvent {
    std::uint64_t sequence{0};
    UiEventPayload payload;
};

/**
 * The only route from worker threads into the UI. Events own all their data;
 * they are drained and consumed by an LVGL timer on the UI thread. Snapshot
 * payloads are coalesced by type: the UI only needs the newest state and a
 * paused UI must not turn periodic providers into an unbounded allocation.
 */
class UiEventQueue {
public:
    void push(UiEvent event);
    std::vector<UiEvent> drain();

private:
    std::mutex mutex_;
    std::deque<UiEvent> events_;
};

}  // namespace micropanel_touch::core
