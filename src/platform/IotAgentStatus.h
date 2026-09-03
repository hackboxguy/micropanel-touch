#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace micropanel_touch::platform {

// What the IOT-Agent screen's indicator shows.
enum class IotAgentStatus {
    unknown,      // not asked yet, or the answer is still on its way
    unreachable,  // the agent's local RPC port did not answer: not running
    offline,      // the agent runs but has no XMPP session
    online,       // the agent has an XMPP session
};

const char* iot_agent_status_name(IotAgentStatus status);

// Polls xmproxysrv's local JSON-RPC port for its XMPP session state. The
// daemon answers `get_online_status` on a plain TCP socket, so this is one
// connect, one line out, one line in.
//
// The poll runs on its own thread, because a connect to a port nobody listens
// on is instant but a connect to a wedged daemon is not, and the UI thread
// draws at a fixed cadence. The thread polls only while somebody is looking:
// each snapshot() renews a short lease, and once the lease lapses the thread
// sleeps until the next snapshot(). That keeps the poll from churning the
// daemon's listener for the whole life of the panel to feed a screen nobody
// has open.
class IotAgentStatusMonitor {
public:
    IotAgentStatusMonitor(std::string host, std::uint16_t port,
                          std::chrono::milliseconds interval = std::chrono::milliseconds(1500),
                          std::chrono::milliseconds lease = std::chrono::seconds(5));
    ~IotAgentStatusMonitor();
    IotAgentStatusMonitor(const IotAgentStatusMonitor&) = delete;
    IotAgentStatusMonitor& operator=(const IotAgentStatusMonitor&) = delete;

    // The latest answer, and a request to keep polling for a while.
    IotAgentStatus snapshot();
    // Forget the last answer, so the next snapshot reports `unknown` until a
    // fresh poll lands. Used after the agent was told to restart, when the
    // old answer describes a session that no longer exists.
    void reset();

    // One poll, synchronously. Exposed for the monitor's own thread and for
    // tests; the timeout bounds connect and reply together.
    static IotAgentStatus probe(const std::string& host, std::uint16_t port,
                                std::chrono::milliseconds timeout);

private:
    void run();

    const std::string host_;
    const std::uint16_t port_;
    const std::chrono::milliseconds interval_;
    const std::chrono::milliseconds lease_;
    std::atomic<IotAgentStatus> status_{IotAgentStatus::unknown};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::chrono::steady_clock::time_point wanted_until_{};
    bool stop_{false};
    std::thread worker_;
};

}  // namespace micropanel_touch::platform
