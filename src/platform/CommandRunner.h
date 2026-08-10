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
};

struct CommandResult {
    CommandStatus status{CommandStatus::start_failed};
    int exit_status{-1};
    std::string output;
};

/**
 * Executes a fixed argv in its own process group. It is deliberately small,
 * but already supplies the lifecycle guarantees needed by UI workers: bounded
 * output, timeout/cancellation, process-group kill, and a guaranteed reap.
 */
class CommandRunner {
public:
    static CommandResult run(const CommandRequest& request,
                             const std::atomic_bool& cancellation_requested);
};

}  // namespace micropanel_touch::platform
