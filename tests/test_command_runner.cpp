#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/CommandRunner.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <unistd.h>

using micropanel_touch::platform::CommandRequest;
using micropanel_touch::platform::CommandRunner;
using micropanel_touch::platform::CommandStatus;

int main() {
    std::atomic_bool cancellation_requested{false};

    const auto success = CommandRunner::run({"/bin/printf", {"hello"}, std::chrono::seconds(1), 64U},
                                            cancellation_requested);
    assert(success.status == CommandStatus::succeeded);
    assert(success.output == "hello");

    const auto missing = CommandRunner::run(
        {"/definitely/not/a-micropanel-touch-command", {}, std::chrono::seconds(1), 64U},
        cancellation_requested);
    assert(missing.status == CommandStatus::start_failed);
    assert(missing.exit_status == 127);

    int stdin_pipe[2];
    assert(pipe(stdin_pipe) == 0);
    const int saved_stdin = dup(STDIN_FILENO);
    assert(saved_stdin >= 0);
    constexpr char kCallerInput[] = "caller-input\n";
    assert(write(stdin_pipe[1], kCallerInput, sizeof(kCallerInput) - 1U) ==
           static_cast<ssize_t>(sizeof(kCallerInput) - 1U));
    assert(dup2(stdin_pipe[0], STDIN_FILENO) == STDIN_FILENO);
    close(stdin_pipe[0]);
    const auto stdin_isolated = CommandRunner::run(
        {"/bin/sh", {"-c", "if IFS= read -r line; then printf 'stdin:%s' \"$line\"; "
                             "else printf stdin-closed; fi"},
         std::chrono::seconds(1), 64U},
        cancellation_requested);
    assert(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);
    close(saved_stdin);
    close(stdin_pipe[1]);
    assert(stdin_isolated.status == CommandStatus::succeeded);
    assert(stdin_isolated.output == "stdin-closed");

    const auto output_limited = CommandRunner::run({"/usr/bin/yes", {}, std::chrono::seconds(1), 64U},
                                                   cancellation_requested);
    assert(output_limited.status == CommandStatus::output_limit_exceeded);
    assert(output_limited.output.size() == 64U);

    const auto timeout_started = std::chrono::steady_clock::now();
    const auto timed_out = CommandRunner::run(
        {"/bin/sh", {"-c", "sleep 2 & wait"}, std::chrono::milliseconds(50), 64U},
        cancellation_requested);
    assert(timed_out.status == CommandStatus::timed_out);
    assert(std::chrono::steady_clock::now() - timeout_started < std::chrono::seconds(2));

    const auto graceful_timeout = CommandRunner::run(
        {"/bin/sh", {"-c", "trap 'printf graceful; exit 0' TERM; while :; do sleep 1; done"},
         std::chrono::milliseconds(50), 64U, std::chrono::milliseconds(500)},
        cancellation_requested);
    assert(graceful_timeout.status == CommandStatus::timed_out);
    assert(graceful_timeout.output.find("graceful") != std::string::npos);

    const auto forced_timeout_started = std::chrono::steady_clock::now();
    const auto forced_timeout = CommandRunner::run(
        {"/bin/sh", {"-c", "trap '' TERM; while :; do sleep 1; done"},
         std::chrono::milliseconds(50), 64U, std::chrono::milliseconds(50)},
        cancellation_requested);
    assert(forced_timeout.status == CommandStatus::timed_out);
    assert(std::chrono::steady_clock::now() - forced_timeout_started < std::chrono::seconds(2));

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
