#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/UiEventQueue.h"
#include "platform/ActionService.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using micropanel_touch::core::ActionProgressUpdate;
using micropanel_touch::core::ActionResultStatus;
using micropanel_touch::core::ActionTerminal;
using micropanel_touch::core::UiEventQueue;
using micropanel_touch::platform::ActionService;
using micropanel_touch::platform::CommandRequest;
using micropanel_touch::platform::CommandService;
using micropanel_touch::platform::ManagedActionRequest;

int main() {
    namespace fs = std::filesystem;
    const fs::path directory = fs::temp_directory_path() /
                               ("micropanel-touch-action-service-" + std::to_string(getpid()));
    fs::create_directory(directory);
    const fs::path log_path = directory / "action.log";

    UiEventQueue queue;
    CommandService command_service(queue);
    ActionService action_service(command_service, queue);

    ManagedActionRequest request;
    request.definition.log_file = "legacy-action.log";
    request.definition.parse_progress = true;
    request.command = {"/bin/sh",
                       {"-c", "printf '%s\\n' 'Progress: 7%'; sleep 0.3; "
                                "printf '%s\\n' 'Progress: 100%' '[SUCCESS] finished'"},
                       std::chrono::seconds(2), 4096U};
    request.managed_log_path = log_path;
    assert(action_service.start(71U, std::move(request)));
    assert(action_service.busy());

    bool observed_live_progress = false;
    bool observed_terminal = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline && !observed_terminal) {
        for (auto& event : queue.drain()) {
            if (const auto* update = std::get_if<ActionProgressUpdate>(&event.payload)) {
                if (update->job_id == 71U && update->progress.progress_percent.has_value() &&
                    *update->progress.progress_percent == 7U) {
                    observed_live_progress = true;
                    assert(update->progress.log_tail.back() == "Progress: 7%");
                }
            }
            if (const auto* terminal = std::get_if<ActionTerminal>(&event.payload)) {
                if (terminal->job_id == 71U) {
                    assert(terminal->result.status == ActionResultStatus::Succeeded);
                    assert(terminal->result.progress_percent == 100U);
                    assert(terminal->result.log_tail.back() == "[SUCCESS] finished");
                    observed_terminal = true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(observed_live_progress);
    assert(observed_terminal);
    assert(!action_service.busy());

    std::ifstream log_stream(log_path);
    const std::string log{std::istreambuf_iterator<char>(log_stream), std::istreambuf_iterator<char>()};
    assert(log == "Progress: 7%\nProgress: 100%\n[SUCCESS] finished\n");
    struct stat metadata {};
    assert(stat(log_path.c_str(), &metadata) == 0);
    assert((metadata.st_mode & 0077) == 0);

    ManagedActionRequest cancellable;
    cancellable.definition.log_file = "cancel.log";
    cancellable.command = {"/bin/sleep", {"2"}, std::chrono::seconds(3), 64U};
    cancellable.managed_log_path = directory / "cancel.log";
    assert(action_service.start(72U, std::move(cancellable)));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    action_service.cancel();
    bool observed_cancellation = false;
    const auto cancellation_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < cancellation_deadline && !observed_cancellation) {
        for (auto& event : queue.drain()) {
            if (const auto* terminal = std::get_if<ActionTerminal>(&event.payload)) {
                if (terminal->job_id == 72U) {
                    assert(terminal->result.status == ActionResultStatus::Cancelled);
                    observed_cancellation = true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(observed_cancellation);
    assert(!action_service.busy());

    ManagedActionRequest missing_target;
    missing_target.definition.log_file = "legacy-action.log";
    missing_target.command = {"/bin/true", {}, std::chrono::seconds(1), 64U};
    assert(!action_service.start(73U, std::move(missing_target)));

    action_service.stop();
    fs::remove_all(directory);
    return 0;
}
