#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

namespace micropanel_touch::core {
class UiEventQueue;
}

namespace micropanel_touch::platform {

// Development-only control endpoint. It is disabled unless main receives an
// explicit --control-socket path, never opens TCP, and accepts only the local
// owner through a 0600 AF_UNIX socket.
class ControlServer {
public:
    explicit ControlServer(core::UiEventQueue& event_queue);
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    bool start(const std::filesystem::path& socket_path, std::string* diagnostic);
    void stop();

private:
    void serve();

    core::UiEventQueue& event_queue_;
    std::atomic_bool running_{false};
    int listen_fd_{-1};
    std::filesystem::path socket_path_;
    std::thread worker_;
    std::atomic_uint64_t next_sequence_{1};
};

}  // namespace micropanel_touch::platform
