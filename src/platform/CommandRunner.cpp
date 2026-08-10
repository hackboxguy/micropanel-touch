#include "platform/CommandRunner.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace micropanel_touch::platform {
namespace {

constexpr int kPollIntervalMilliseconds = 50;

void signal_process_group(pid_t process_group, int signal) {
    if (kill(-process_group, signal) != 0) {
        kill(process_group, signal);
    }
}

bool set_close_on_exec(int file_descriptor) {
    const int flags = fcntl(file_descriptor, F_GETFD);
    return flags != -1 && fcntl(file_descriptor, F_SETFD, flags | FD_CLOEXEC) != -1;
}

void report_start_failure(int file_descriptor, unsigned char marker) {
    // This is intentionally a fixed one-byte protocol: after fork, before
    // exec, only async-signal-safe operations are permitted in this worker.
    while (write(file_descriptor, &marker, sizeof(marker)) == -1 && errno == EINTR) {
    }
}

bool child_reported_start_failure(int file_descriptor) {
    unsigned char marker = 0;
    for (;;) {
        const ssize_t count = read(file_descriptor, &marker, sizeof(marker));
        if (count > 0) {
            return true;
        }
        if (count == 0 || (count < 0 && errno != EINTR)) {
            return false;
        }
    }
}

bool reap_nonblocking(pid_t child, int* status) {
    for (;;) {
        const pid_t waited = waitpid(child, status, WNOHANG);
        if (waited == child) {
            return true;
        }
        if (waited == 0) {
            return false;
        }
        if (errno != EINTR) {
            *status = -1;
            return true;
        }
    }
}

}  // namespace

CommandResult CommandRunner::run(const CommandRequest& request,
                                 const std::atomic_bool& cancellation_requested,
                                 CommandOutputObserver output_observer) {
    CommandResult result;
    if (request.executable.empty() || request.timeout.count() <= 0) {
        return result;
    }

    // Build argv before fork: callers run this from workers while the app has
    // other threads, and the child may only use async-signal-safe operations.
    std::vector<char*> argv;
    argv.reserve(request.arguments.size() + 2U);
    argv.push_back(const_cast<char*>(request.executable.c_str()));
    for (const std::string& argument : request.arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return result;
    }
    int start_failure_fds[2]{-1, -1};
    if (pipe(start_failure_fds) != 0 || !set_close_on_exec(start_failure_fds[1])) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        if (start_failure_fds[0] >= 0) {
            close(start_failure_fds[0]);
        }
        if (start_failure_fds[1] >= 0) {
            close(start_failure_fds[1]);
        }
        return result;
    }

    const pid_t child = fork();
    if (child == -1) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        close(start_failure_fds[0]);
        close(start_failure_fds[1]);
        return result;
    }
    if (child == 0) {
        close(start_failure_fds[0]);
        const int null_input = open("/dev/null", O_RDONLY);
        if (setpgid(0, 0) != 0 || null_input == -1 ||
            dup2(null_input, STDIN_FILENO) == -1 ||
            dup2(pipe_fds[1], STDOUT_FILENO) == -1 ||
            dup2(pipe_fds[1], STDERR_FILENO) == -1) {
            report_start_failure(start_failure_fds[1], 126U);
            _exit(126);
        }
        if (null_input != STDIN_FILENO) {
            close(null_input);
        }
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execv(request.executable.c_str(), argv.data());
        report_start_failure(start_failure_fds[1], 127U);
        _exit(127);
    }

    close(pipe_fds[1]);
    close(start_failure_fds[1]);
    // Close the race where cancellation arrives before the child calls setpgid.
    setpgid(child, child);
    const int flags = fcntl(pipe_fds[0], F_GETFL);
    if (flags == -1 || fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        close(pipe_fds[0]);
        close(start_failure_fds[0]);
        signal_process_group(child, SIGKILL);
        while (waitpid(child, nullptr, 0) == -1 && errno == EINTR) {
        }
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + request.timeout;
    int pipe_fd = pipe_fds[0];
    bool child_reaped = false;
    bool termination_requested = false;
    bool force_kill_sent = false;
    std::chrono::steady_clock::time_point force_kill_deadline;
    int wait_status = -1;
    std::array<char, 4096> buffer{};

    const auto begin_termination = [&](CommandStatus status) {
        result.status = status;
        termination_requested = true;
        signal_process_group(child, SIGTERM);
        if (request.termination_grace.count() <= 0) {
            signal_process_group(child, SIGKILL);
            force_kill_sent = true;
        } else {
            force_kill_deadline = std::chrono::steady_clock::now() + request.termination_grace;
        }
    };

    while (pipe_fd >= 0 || !child_reaped) {
        const auto now = std::chrono::steady_clock::now();
        if (!termination_requested && cancellation_requested.load()) {
            begin_termination(CommandStatus::cancelled);
        } else if (!termination_requested && now >= deadline) {
            begin_termination(CommandStatus::timed_out);
        } else if (termination_requested && !force_kill_sent && now >= force_kill_deadline) {
            signal_process_group(child, SIGKILL);
            force_kill_sent = true;
        }

        int poll_timeout = kPollIntervalMilliseconds;
        if (!force_kill_sent) {
            const auto next_deadline = termination_requested ? force_kill_deadline : deadline;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                next_deadline - std::chrono::steady_clock::now());
            poll_timeout = static_cast<int>(std::max<std::int64_t>(
                0, std::min<std::int64_t>(remaining.count(), kPollIntervalMilliseconds)));
        }

        if (pipe_fd >= 0) {
            pollfd descriptor{pipe_fd, POLLIN | POLLHUP, 0};
            if (poll(&descriptor, 1, poll_timeout) > 0) {
                for (;;) {
                    const ssize_t count = read(pipe_fd, buffer.data(), buffer.size());
                    if (count > 0) {
                        const std::size_t remaining = request.maximum_output_bytes - result.output.size();
                        const std::size_t captured = std::min<std::size_t>(remaining, count);
                        result.output.append(buffer.data(), captured);
                        if (captured > 0U && output_observer) {
                            output_observer(std::string_view(buffer.data(), captured));
                        }
                        if (result.output.size() == request.maximum_output_bytes &&
                            !termination_requested) {
                            begin_termination(CommandStatus::output_limit_exceeded);
                        }
                        // Do not drain an always-readable pipe indefinitely:
                        // a TERM-ignoring producer must still reach SIGKILL at
                        // the grace deadline.
                        if (termination_requested) {
                            break;
                        }
                        continue;
                    }
                    if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                                       errno != EINTR)) {
                        close(pipe_fd);
                        pipe_fd = -1;
                        break;
                    }
                    if (count < 0 && errno == EINTR) {
                        continue;
                    }
                    break;
                }
            }
        } else {
            poll(nullptr, 0, poll_timeout);
        }
        child_reaped = reap_nonblocking(child, &wait_status) || child_reaped;
    }

    const bool start_failed = child_reported_start_failure(start_failure_fds[0]);
    close(start_failure_fds[0]);
    if (WIFEXITED(wait_status)) {
        result.exit_status = WEXITSTATUS(wait_status);
    }
    if (WIFSIGNALED(wait_status)) {
        result.terminating_signal = WTERMSIG(wait_status);
    }
    if (start_failed) {
        result.status = CommandStatus::start_failed;
    } else if (!termination_requested) {
        if (WIFSIGNALED(wait_status)) {
            result.status = CommandStatus::killed;
        } else {
            result.status = WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0
                ? CommandStatus::succeeded
                : CommandStatus::failed;
        }
    }
    return result;
}

}  // namespace micropanel_touch::platform
