#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/IotAgentStatus.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using micropanel_touch::platform::IotAgentStatus;
using micropanel_touch::platform::IotAgentStatusMonitor;

// A stand-in xmproxysrv: answers every connection with one fixed reply line.
class FakeAgent {
public:
    explicit FakeAgent(std::string reply) : reply_(std::move(reply)) {
        listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        assert(listen_fd_ >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        assert(bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
        socklen_t length = sizeof(address);
        assert(getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length) == 0);
        port_ = ntohs(address.sin_port);
        assert(listen(listen_fd_, 4) == 0);
        worker_ = std::thread([this] { serve(); });
    }
    ~FakeAgent() {
        stop_.store(true);
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        worker_.join();
    }
    std::uint16_t port() const { return port_; }
    unsigned int requests() const { return requests_.load(); }

private:
    void serve() {
        while (!stop_.load()) {
            const int client = accept(listen_fd_, nullptr, nullptr);
            if (client < 0) {
                return;
            }
            char buffer[512];
            std::string request;
            while (request.find('\n') == std::string::npos) {
                const ssize_t count = recv(client, buffer, sizeof(buffer), 0);
                if (count <= 0) {
                    break;
                }
                request.append(buffer, static_cast<std::size_t>(count));
            }
            assert(request.find("get_online_status") != std::string::npos);
            ++requests_;
            (void)send(client, reply_.data(), reply_.size(), MSG_NOSIGNAL);
            close(client);
        }
    }

    std::string reply_;
    int listen_fd_{-1};
    std::uint16_t port_{0};
    std::atomic_bool stop_{false};
    std::atomic<unsigned int> requests_{0U};
    std::thread worker_;
};

std::uint16_t unused_port() {
    // Bind, read the port back, close: nothing listens there afterwards.
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    socklen_t length = sizeof(address);
    assert(getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    close(fd);
    return ntohs(address.sin_port);
}

IotAgentStatus wait_for_status(IotAgentStatusMonitor& monitor, IotAgentStatus expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    IotAgentStatus status = monitor.snapshot();
    while (status != expected && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        status = monitor.snapshot();
    }
    return status;
}

}  // namespace

int main() {
    using namespace std::chrono_literals;

    // The synchronous probe, against each kind of answer.
    {
        const FakeAgent online(
            "{ \"jsonrpc\": \"2.0\", \"result\": { \"status\": \"online\" }, \"id\": 0 }\n");
        assert(IotAgentStatusMonitor::probe("127.0.0.1", online.port(), 1000ms) ==
               IotAgentStatus::online);
    }
    {
        const FakeAgent offline(
            "{ \"jsonrpc\": \"2.0\", \"result\": { \"status\": \"offline\" }, \"id\": 0 }\n");
        assert(IotAgentStatusMonitor::probe("127.0.0.1", offline.port(), 1000ms) ==
               IotAgentStatus::offline);
    }
    {
        const FakeAgent garbage("not json at all\n");
        assert(IotAgentStatusMonitor::probe("127.0.0.1", garbage.port(), 1000ms) ==
               IotAgentStatus::unreachable);
    }
    assert(IotAgentStatusMonitor::probe("127.0.0.1", unused_port(), 1000ms) ==
           IotAgentStatus::unreachable);
    assert(IotAgentStatusMonitor::probe("not-an-address", 1, 100ms) ==
           IotAgentStatus::unreachable);

    // The monitor: unknown until asked, polls while asked, stops when not.
    {
        FakeAgent agent(
            "{ \"jsonrpc\": \"2.0\", \"result\": { \"status\": \"online\" }, \"id\": 0 }\n");
        IotAgentStatusMonitor monitor("127.0.0.1", agent.port(), 50ms, 200ms);
        // Nobody has asked yet: no traffic.
        std::this_thread::sleep_for(150ms);
        assert(agent.requests() == 0U);
        assert(monitor.snapshot() == IotAgentStatus::unknown);
        assert(wait_for_status(monitor, IotAgentStatus::online) == IotAgentStatus::online);
        // Stop asking: the lease lapses and polling stops.
        std::this_thread::sleep_for(400ms);
        const unsigned int after_lease = agent.requests();
        std::this_thread::sleep_for(300ms);
        assert(agent.requests() == after_lease);
        // reset() forgets the answer; the next snapshot refreshes it.
        monitor.reset();
        assert(monitor.snapshot() == IotAgentStatus::unknown);
        assert(wait_for_status(monitor, IotAgentStatus::online) == IotAgentStatus::online);
    }
    // A monitor pointed at nothing reports unreachable, and shuts down
    // promptly while it is still polling.
    {
        IotAgentStatusMonitor monitor("127.0.0.1", unused_port(), 50ms, 5000ms);
        assert(wait_for_status(monitor, IotAgentStatus::unreachable) ==
               IotAgentStatus::unreachable);
        const auto started = std::chrono::steady_clock::now();
        {
            IotAgentStatusMonitor busy("127.0.0.1", unused_port(), 50ms, 5000ms);
            (void)busy.snapshot();
            std::this_thread::sleep_for(60ms);
        }
        assert(std::chrono::steady_clock::now() - started < 2s);
    }
    (void)micropanel_touch::platform::iot_agent_status_name(IotAgentStatus::online);
    return 0;
}
