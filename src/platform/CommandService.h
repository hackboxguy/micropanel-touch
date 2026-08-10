#pragma once

#include "core/UiEventQueue.h"
#include "platform/CommandRunner.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>

namespace micropanel_touch::platform {

struct CommandCallbacks {
    // Both callbacks run on CommandService's worker thread. They must only
    // manage owned data or enqueue immutable events; LVGL remains UI-thread
    // only. The ordinary CommandCompletion is still always queued afterwards.
    CommandOutputObserver on_output;
    std::function<void(const core::CommandCompletion& completion)> on_completion;
};

/**
 * Owns the one v1 external-command job and reports its terminal state through
 * UiEventQueue. It deliberately exposes no UI API: workers only enqueue data;
 * the LVGL thread decides whether the result still belongs to its screen.
 */
class CommandService {
public:
    explicit CommandService(core::UiEventQueue& event_queue);
    ~CommandService();
    CommandService(const CommandService&) = delete;
    CommandService& operator=(const CommandService&) = delete;

    // Returns false when a job is already live or the request is malformed.
    bool start(std::uint64_t job_id, CommandRequest request,
               CommandCallbacks callbacks = {});
    void cancel();
    void stop();
    bool busy() const;

private:
    void run(std::uint64_t sequence, std::uint64_t job_id, CommandRequest request,
             CommandCallbacks callbacks);

    core::UiEventQueue& event_queue_;
    mutable std::mutex mutex_;
    std::atomic_bool cancellation_requested_{false};
    bool busy_{false};
    std::uint64_t next_sequence_{1};
    std::thread worker_;
};

}  // namespace micropanel_touch::platform
