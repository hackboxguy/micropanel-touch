#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/CommandRunner.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <csignal>
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

    // A supplied stdin reaches the child, and only what was supplied: the
    // caller's own stdin stays isolated either way.
    CommandRequest with_input{"/bin/sh",
                              {"-c", "read -r line; printf 'got:%s' \"$line\"; "
                                     "if IFS= read -r extra; then printf ':more'; fi"},
                              std::chrono::seconds(1), 64U};
    with_input.standard_input = "s3cret-passphrase\n";
    const auto supplied_input = CommandRunner::run(with_input, cancellation_requested);
    assert(supplied_input.status == CommandStatus::succeeded);
    assert(supplied_input.output == "got:s3cret-passphrase");

    // The reason stdin exists here at all: the secret must not be reachable
    // through the child's command line, where any local user could read it.
    CommandRequest secret_not_in_argv{"/bin/sh",
                                      {"-c", "tr '\\0' ' ' < /proc/self/cmdline"},
                                      std::chrono::seconds(1), 256U};
    secret_not_in_argv.standard_input = "s3cret-passphrase\n";
    const auto argv_view = CommandRunner::run(secret_not_in_argv, cancellation_requested);
    assert(argv_view.status == CommandStatus::succeeded);
    assert(argv_view.output.find("s3cret-passphrase") == std::string::npos);

    // A child that never reads its input must still finish rather than
    // deadlock a parent waiting to hand it over.
    CommandRequest unread_input{"/bin/printf", {"ignored"}, std::chrono::seconds(1), 64U};
    unread_input.standard_input = std::string(256U * 1024U, 'x');
    const auto ignored_input = CommandRunner::run(unread_input, cancellation_requested);
    assert(ignored_input.status == CommandStatus::succeeded);
    assert(ignored_input.output == "ignored");

    const auto output_limited = CommandRunner::run({"/usr/bin/yes", {}, std::chrono::seconds(1), 64U},
                                                   cancellation_requested);
    assert(output_limited.status == CommandStatus::output_limit_exceeded);
    assert(output_limited.output.size() == 64U);

    const auto killed = CommandRunner::run(
        {"/bin/sh", {"-c", "kill -KILL $$"}, std::chrono::seconds(1), 64U},
        cancellation_requested);
    assert(killed.status == CommandStatus::killed);
    assert(killed.terminating_signal == SIGKILL);

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
