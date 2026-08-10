#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/ActionCompiler.h"
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

using micropanel_touch::core::ActionCompiler;
using micropanel_touch::core::ActionProgressUpdate;
using micropanel_touch::core::ActionResultStatus;
using micropanel_touch::core::ActionTerminal;
using micropanel_touch::core::CommandCompletion;
using micropanel_touch::core::ExecutionContext;
using micropanel_touch::core::UiEventQueue;
using micropanel_touch::platform::ActionService;
using micropanel_touch::platform::CommandService;

namespace {

ExecutionContext test_context(const std::filesystem::path& root,
                              const std::filesystem::path& source_root) {
    return {
        source_root,
        source_root / "screens",
        root / "data",
        root / "data" / "logs",
        root / "runtime",
        source_root / "handlers",
    };
}

}  // namespace

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;
    assert(argc == 2);
    const fs::path directory = fs::temp_directory_path() /
                               ("micropanel-touch-action-service-" + std::to_string(getpid()));
    fs::create_directory(directory);
    const fs::path source_root = argv[1];
    const ExecutionContext context = test_context(directory, source_root);
    std::string diagnostic;
    assert(context.validate(&diagnostic));
    fs::create_directories(context.log_dir);

    UiEventQueue queue;
    CommandService command_service(queue);
    ActionService action_service(command_service, queue);

    auto action = ActionCompiler::compile_native("demo.simulated-flash", context, &diagnostic);
    assert(action.has_value());
    assert(action_service.start(71U, std::move(*action)));
    assert(action_service.busy());

    bool observed_live_progress = false;
    bool observed_terminal = false;
    bool observed_generic_completion = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline && !observed_terminal) {
        for (auto& event : queue.drain()) {
            if (const auto* completion = std::get_if<CommandCompletion>(&event.payload)) {
                if (completion->job_id == 71U) {
                    observed_generic_completion = true;
                }
            }
            if (const auto* update = std::get_if<ActionProgressUpdate>(&event.payload)) {
                if (update->job_id == 71U && update->progress.progress_percent.has_value() &&
                    *update->progress.progress_percent == 20U) {
                    observed_live_progress = true;
                    assert(update->progress.log_tail.back() == "Progress: 20%");
                }
            }
            if (const auto* terminal = std::get_if<ActionTerminal>(&event.payload)) {
                if (terminal->job_id == 71U) {
                    assert(terminal->result.status == ActionResultStatus::Succeeded);
                    assert(terminal->result.progress_percent == 100U);
                    assert(terminal->result.log_tail.back() == "[SUCCESS] simulated flash complete");
                    observed_terminal = true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(observed_live_progress);
    assert(observed_terminal);
    assert(!observed_generic_completion);
    assert(!action_service.busy());

    const fs::path log_path = context.log_dir / "simulated-flash.log";
    std::ifstream log_stream(log_path);
    const std::string log{std::istreambuf_iterator<char>(log_stream), std::istreambuf_iterator<char>()};
    assert(log.find("Progress: 0%\nProgress: 20%") == 0U);
    assert(log.find("[SUCCESS] simulated flash complete\n") != std::string::npos);
    struct stat metadata {};
    assert(stat(log_path.c_str(), &metadata) == 0);
    assert((metadata.st_mode & 0077) == 0);

    auto cancellable = ActionCompiler::compile_native("demo.simulated-flash", context, &diagnostic);
    assert(cancellable.has_value());
    assert(action_service.start(72U, std::move(*cancellable)));
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

    ExecutionContext missing_log_directory = test_context(directory / "missing", source_root);
    assert(missing_log_directory.validate(&diagnostic));
    auto missing_target =
        ActionCompiler::compile_native("demo.simulated-flash", missing_log_directory, &diagnostic);
    assert(missing_target.has_value());
    assert(!action_service.start(73U, std::move(*missing_target)));

    action_service.stop();
    fs::remove_all(directory);
    return 0;
}
