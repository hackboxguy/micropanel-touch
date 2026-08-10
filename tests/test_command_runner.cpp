#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/CommandRunner.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

using micropanel_touch::platform::CommandRequest;
using micropanel_touch::platform::CommandRunner;
using micropanel_touch::platform::CommandStatus;

int main() {
    std::atomic_bool cancellation_requested{false};

    const auto success = CommandRunner::run({"/bin/printf", {"hello"}, std::chrono::seconds(1), 64U},
                                            cancellation_requested);
    assert(success.status == CommandStatus::succeeded);
    assert(success.output == "hello");

    const auto output_limited = CommandRunner::run({"/usr/bin/yes", {}, std::chrono::seconds(1), 64U},
                                                   cancellation_requested);
    assert(output_limited.status == CommandStatus::output_limit_exceeded);
    assert(output_limited.output.size() == 64U);

    const auto timeout_started = std::chrono::steady_clock::now();
    const auto timed_out = CommandRunner::run(
        {"/bin/sh", {"-c", "sleep 2 & wait"}, std::chrono::milliseconds(50), 64U},
        cancellation_requested);
    assert(timed_out.status == CommandStatus::timed_out);
    assert(std::chrono::steady_clock::now() - timeout_started < std::chrono::milliseconds(500));

    std::thread canceller([&cancellation_requested] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        cancellation_requested.store(true);
    });
    const auto cancelled = CommandRunner::run({"/bin/sleep", {"2"}, std::chrono::seconds(1), 64U},
                                              cancellation_requested);
    canceller.join();
    assert(cancelled.status == CommandStatus::cancelled);
    return 0;
}
