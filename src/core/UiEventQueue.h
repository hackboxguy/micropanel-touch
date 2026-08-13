#pragma once

#include "core/ActionRunner.h"
#include "core/UiControl.h"

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

// The saved NetworkManager IPv4 configuration for the one interface managed
// by the appliance. This deliberately remains separate from NetworkSnapshot:
// link/address discovery is inexpensive and frequent, whereas reading a
// connection profile is requested only when the IP settings page opens.
struct ManagedIpv4Profile {
    std::string interface_name;
    std::string method;
    std::string address_with_prefix;
    std::string gateway;
    // The root-owned DHCP-server state file is cross-checked against this
    // NetworkManager profile before it is exposed to the UI. These fields let
    // the selector restore its distinct server mode after reboot/link loss.
    bool dhcp_server_active{false};
    std::string dhcp_server_lease_start;
    std::string dhcp_server_lease_end;
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

enum class CommandCompletionStatus {
    Succeeded,
    Failed,
    Killed,
    TimedOut,
    Cancelled,
    OutputLimitExceeded,
    StartFailed,
};

// A completion is ordered rather than replaceable: a later job must not hide
// the terminal outcome of an earlier one before its owner has consumed it.
struct CommandCompletion {
    std::uint64_t job_id{0};
    CommandCompletionStatus status{CommandCompletionStatus::StartFailed};
    int exit_status{-1};
    std::string output;
    // Non-zero only when the direct child exited due to a signal. A
    // cancellation or timeout can also retain its final signal as diagnostic
    // data, while the status remains Cancelled/TimedOut.
    int terminating_signal{0};
};

struct ActionProgressUpdate {
    std::uint64_t job_id{0};
    ActionProgress progress;
};

struct ActionTerminal {
    std::uint64_t job_id{0};
    ActionResult result;
};

// A network-settings request may wait for a root-owned broker and NetworkManager.
// Carry its terminal result back to LVGL as immutable data, just like an
// action result; the worker that contacted the broker never touches widgets.
struct NetworkApplyResult {
    std::uint64_t request_id{0};
    bool ok{false};
    std::string message;
};

using UiEventPayload = std::variant<NetworkSnapshot, ManagedIpv4Profile, WifiScanResult,
                                    CommandCompletion, ActionProgressUpdate, ActionTerminal,
                                    NetworkApplyResult, UiControlRequest>;

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
