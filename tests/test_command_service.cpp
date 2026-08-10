#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/UiEventQueue.h"
#include "platform/CommandService.h"

#include <cassert>
#include <chrono>
#include <csignal>
#include <thread>
#include <vector>

using micropanel_touch::core::CommandCompletion;
using micropanel_touch::core::CommandCompletionStatus;
using micropanel_touch::core::UiEventQueue;
using micropanel_touch::platform::CommandRequest;
using micropanel_touch::platform::CommandService;

namespace {

CommandCompletion wait_for_completion(UiEventQueue& queue, std::uint64_t job_id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& event : queue.drain()) {
            if (auto* completion = std::get_if<CommandCompletion>(&event.payload)) {
                if (completion->job_id == job_id) {
                    return std::move(*completion);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(false && "timed out waiting for command completion");
    return {};
}

}  // namespace

int main() {
    UiEventQueue queue;
    CommandService service(queue);

    assert(service.start(1U, {"/bin/printf", {"done"}, std::chrono::seconds(1), 64U}));
    const CommandCompletion success = wait_for_completion(queue, 1U);
    assert(success.status == CommandCompletionStatus::Succeeded);
    assert(success.exit_status == 0);
    assert(success.output == "done");
    assert(!service.busy());

    assert(service.start(2U, {"/bin/sleep", {"2"}, std::chrono::seconds(3), 64U}));
    assert(service.busy());
    assert(!service.start(3U, {"/bin/true", {}, std::chrono::seconds(1), 64U}));
    service.cancel();
    const CommandCompletion cancelled = wait_for_completion(queue, 2U);
    assert(cancelled.status == CommandCompletionStatus::Cancelled);
    assert(!service.busy());

    assert(service.start(4U, {"/not/a-command", {}, std::chrono::seconds(1), 64U}));
    const CommandCompletion failed_start = wait_for_completion(queue, 4U);
    assert(failed_start.status == CommandCompletionStatus::StartFailed);
    assert(failed_start.exit_status == 127);

    assert(service.start(5U, {"/bin/sh", {"-c", "kill -TERM $$"}, std::chrono::seconds(1), 64U}));
    const CommandCompletion killed = wait_for_completion(queue, 5U);
    assert(killed.status == CommandCompletionStatus::Killed);
    assert(killed.terminating_signal == SIGTERM);

    assert(!service.start(0U, {"/bin/true", {}, std::chrono::seconds(1), 64U}));
    assert(!service.start(6U, {"", {}, std::chrono::seconds(1), 64U}));
    return 0;
}
