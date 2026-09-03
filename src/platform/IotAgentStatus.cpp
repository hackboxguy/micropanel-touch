#include "platform/IotAgentStatus.h"

#include <cerrno>
#include <cstring>
#include <string_view>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace micropanel_touch::platform {
namespace {

constexpr std::string_view kRequest =
    "{ \"jsonrpc\": \"2.0\", \"method\": \"get_online_status\", \"id\": 0 }\n";

class Socket {
public:
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    int get() const { return fd_; }

private:
    int fd_;
};

std::chrono::milliseconds remaining(std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
}

bool wait_for(int fd, short events, std::chrono::steady_clock::time_point deadline) {
    pollfd descriptor{fd, events, 0};
    while (true) {
        const auto left = remaining(deadline);
        if (left.count() == 0) {
            return false;
        }
        const int ready = ::poll(&descriptor, 1, static_cast<int>(left.count()));
        if (ready > 0) {
            return (descriptor.revents & (events | POLLERR | POLLHUP)) != 0;
        }
        if (ready == 0 || errno != EINTR) {
            return false;
        }
    }
}

}  // namespace

const char* iot_agent_status_name(IotAgentStatus status) {
    switch (status) {
        case IotAgentStatus::online:
            return "online";
        case IotAgentStatus::offline:
            return "offline";
        case IotAgentStatus::unreachable:
            return "unreachable";
        case IotAgentStatus::unknown:
        default:
            return "unknown";
    }
}

IotAgentStatus IotAgentStatusMonitor::probe(const std::string& host, std::uint16_t port,
                                            std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        return IotAgentStatus::unreachable;
    }
    const Socket socket(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (socket.get() < 0) {
        return IotAgentStatus::unreachable;
    }
    if (::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        if (errno != EINPROGRESS) {
            return IotAgentStatus::unreachable;
        }
        if (!wait_for(socket.get(), POLLOUT, deadline)) {
            return IotAgentStatus::unreachable;
        }
        int error = 0;
        socklen_t length = sizeof(error);
        if (::getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0) {
            return IotAgentStatus::unreachable;
        }
    }
    std::size_t sent = 0U;
    while (sent < kRequest.size()) {
        const ssize_t count = ::send(socket.get(), kRequest.data() + sent, kRequest.size() - sent,
                                     MSG_NOSIGNAL);
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_for(socket.get(), POLLOUT, deadline)) {
                return IotAgentStatus::unreachable;
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        return IotAgentStatus::unreachable;
    }
    std::string reply;
    char buffer[512];
    while (reply.find('\n') == std::string::npos && reply.size() < 4096U) {
        if (!wait_for(socket.get(), POLLIN, deadline)) {
            break;
        }
        const ssize_t count = ::recv(socket.get(), buffer, sizeof(buffer), 0);
        if (count > 0) {
            reply.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        break;
    }
    // The daemon's reply is a JSON-RPC result whose status value is one of
    // the words the daemon's own tests look for. Nothing else in the reply
    // contains either word, so a substring test is the whole parser.
    if (reply.find("\"online\"") != std::string::npos) {
        return IotAgentStatus::online;
    }
    if (reply.find("\"offline\"") != std::string::npos) {
        return IotAgentStatus::offline;
    }
    return IotAgentStatus::unreachable;
}

IotAgentStatusMonitor::IotAgentStatusMonitor(std::string host, std::uint16_t port,
                                             std::chrono::milliseconds interval,
                                             std::chrono::milliseconds lease)
    : host_(std::move(host)), port_(port), interval_(interval), lease_(lease),
      worker_([this] { run(); }) {}

IotAgentStatusMonitor::~IotAgentStatusMonitor() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

IotAgentStatus IotAgentStatusMonitor::snapshot() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        wanted_until_ = std::chrono::steady_clock::now() + lease_;
    }
    wake_.notify_all();
    return status_.load();
}

void IotAgentStatusMonitor::reset() {
    status_.store(IotAgentStatus::unknown);
}

void IotAgentStatusMonitor::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        if (std::chrono::steady_clock::now() >= wanted_until_) {
            wake_.wait(lock, [this] {
                return stop_ || std::chrono::steady_clock::now() < wanted_until_;
            });
            continue;
        }
        lock.unlock();
        // Shorter than the interval, so a wedged daemon cannot make one poll
        // overlap the next.
        const IotAgentStatus status = probe(host_, port_, interval_ * 2 / 3);
        status_.store(status);
        lock.lock();
        wake_.wait_for(lock, interval_, [this] { return stop_; });
    }
}

}  // namespace micropanel_touch::platform
