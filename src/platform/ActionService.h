#pragma once

#include "core/ActionCompiler.h"
#include "core/UiEventQueue.h"
#include "platform/CommandService.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

namespace micropanel_touch::platform {

/**
 * Bridges the one CommandService worker to ActionRunner's compatibility
 * semantics. Output is written once to a caller-resolved managed file and
 * sampled into immutable progress events; this class never touches LVGL.
 */
class ActionService {
public:
    ActionService(CommandService& command_service, core::UiEventQueue& event_queue);
    ~ActionService();
    ActionService(const ActionService&) = delete;
    ActionService& operator=(const ActionService&) = delete;

    // The private VettedAction constructor ensures that only ActionCompiler's
    // reviewed allowlist can reach this execution boundary.
    bool start(std::uint64_t job_id, core::VettedAction action);
    void cancel();
    // Called by the UI's low-rate progress timer for duration-estimated jobs.
    // It enqueues a snapshot; it does not call LVGL.
    void refresh_progress(std::uint64_t job_id);
    void stop();
    bool busy() const;

private:
    struct State;

    void handle_output(std::uint64_t job_id, std::string_view output);
    void handle_completion(const core::CommandCompletion& completion);
    void publish_progress(std::uint64_t job_id, bool force);

    CommandService& command_service_;
    core::UiEventQueue& event_queue_;
    mutable std::mutex mutex_;
    std::unique_ptr<State> active_;
    std::atomic<std::uint64_t> next_sequence_{1U};
};

}  // namespace micropanel_touch::platform
