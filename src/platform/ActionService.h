#pragma once

#include "core/ActionRunner.h"
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
 * A runnable action contains a fixed argv request and an already-resolved
 * managed log path. It deliberately has no raw legacy action string: config
 * compilation must happen at the allowlisted boundary before this API.
 */
struct ManagedActionRequest {
    core::ActionDefinition definition;
    CommandRequest command;
    std::optional<std::filesystem::path> managed_log_path;
};

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

    // Returns false for a malformed request, a missing managed log target, or
    // an already-live action. The request's legacy log_file is metadata only;
    // it is never used as a filesystem path here.
    bool start(std::uint64_t job_id, ManagedActionRequest request);
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
