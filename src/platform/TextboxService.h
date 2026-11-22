#pragma once

#include "core/UiEventQueue.h"
#include "platform/CommandRunner.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace micropanel_touch::platform {

/**
 * Runs the script behind a JSON `textbox` screen, one run at a time, on its
 * own worker so a slow or hung script never stalls LVGL. The result is posted
 * to UiEventQueue as core::TextboxOutput; the UI thread decides whether it
 * still belongs to the screen on display (request ids are never reused).
 *
 * Deliberately separate from CommandService: that slot is shared by the
 * operator-started jobs, and a status screen refreshing every few seconds
 * must neither block them nor be blocked by them.
 */
class TextboxService {
public:
    explicit TextboxService(core::UiEventQueue& event_queue);
    ~TextboxService();
    TextboxService(const TextboxService&) = delete;
    TextboxService& operator=(const TextboxService&) = delete;

    // Returns false (and runs nothing) while the previous run is still live:
    // a refresh tick that finds the script still running is simply skipped.
    bool request(std::uint64_t request_id, CommandRequest request);
    // Cancels a live run (process group SIGTERM -> SIGKILL) and joins it.
    void stop();
    bool busy() const;

private:
    void run(std::uint64_t request_id, CommandRequest request);

    core::UiEventQueue& event_queue_;
    mutable std::mutex mutex_;
    std::atomic_bool cancel_{false};
    bool busy_{false};
    std::uint64_t next_sequence_{1};
    std::thread worker_;
};

}  // namespace micropanel_touch::platform
