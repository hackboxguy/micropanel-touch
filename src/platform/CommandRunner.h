#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace micropanel_touch::platform {

enum class CommandStatus {
    succeeded,
    failed,
    timed_out,
    cancelled,
    output_limit_exceeded,
    start_failed,
};

struct CommandRequest {
    std::string executable;
    std::vector<std::string> arguments;
    std::chrono::milliseconds timeout{15000};
    std::size_t maximum_output_bytes{64U * 1024U};
    // A zero grace period preserves immediate process-group SIGKILL behavior.
    std::chrono::milliseconds termination_grace{1500};
};

struct CommandResult {
    CommandStatus status{CommandStatus::start_failed};
    int exit_status{-1};
    std::string output;
};

/**
 * Executes a fixed argv in its own process group. It is deliberately small,
 * but already supplies the lifecycle guarantees needed by UI workers: bounded
 * output, timeout/cancellation, process-group SIGTERM-to-SIGKILL escalation,
 * and a guaranteed reap. A child stuck in uninterruptible kernel sleep cannot
 * be reaped until the kernel releases it, so run() intentionally keeps waiting
 * rather than reporting a false completion.
 */
class CommandRunner {
public:
    static CommandResult run(const CommandRequest& request,
                             const std::atomic_bool& cancellation_requested);
};

}  // namespace micropanel_touch::platform
