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

void kill_process_group(pid_t process_group) {
    if (kill(-process_group, SIGKILL) != 0) {
        kill(process_group, SIGKILL);
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
                                 const std::atomic_bool& cancellation_requested) {
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

    const pid_t child = fork();
    if (child == -1) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return result;
    }
    if (child == 0) {
        if (setpgid(0, 0) != 0 || dup2(pipe_fds[1], STDOUT_FILENO) == -1 ||
            dup2(pipe_fds[1], STDERR_FILENO) == -1) {
            _exit(126);
        }
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execv(request.executable.c_str(), argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    // Close the race where cancellation arrives before the child calls setpgid.
    setpgid(child, child);
    const int flags = fcntl(pipe_fds[0], F_GETFL);
    if (flags == -1 || fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        close(pipe_fds[0]);
        kill_process_group(child);
        while (waitpid(child, nullptr, 0) == -1 && errno == EINTR) {
        }
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + request.timeout;
    int pipe_fd = pipe_fds[0];
    bool child_reaped = false;
    bool terminated = false;
    int wait_status = -1;
    std::array<char, 4096> buffer{};

    while (pipe_fd >= 0 || !child_reaped) {
        if (!terminated && cancellation_requested.load()) {
            kill_process_group(child);
            result.status = CommandStatus::cancelled;
            terminated = true;
        } else if (!terminated && std::chrono::steady_clock::now() >= deadline) {
            kill_process_group(child);
            result.status = CommandStatus::timed_out;
            terminated = true;
        }

        int poll_timeout = kPollIntervalMilliseconds;
        if (!terminated) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
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
                        result.output.append(buffer.data(), std::min<std::size_t>(remaining, count));
                        if (result.output.size() == request.maximum_output_bytes && !terminated) {
                            kill_process_group(child);
                            result.status = CommandStatus::output_limit_exceeded;
                            terminated = true;
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

    if (!terminated) {
        result.status = WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0
            ? CommandStatus::succeeded
            : CommandStatus::failed;
    }
    if (WIFEXITED(wait_status)) {
        result.exit_status = WEXITSTATUS(wait_status);
    }
    return result;
}

}  // namespace micropanel_touch::platform
