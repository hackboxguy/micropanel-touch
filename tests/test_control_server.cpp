#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/UiControl.h"
#include "core/UiEventQueue.h"
#include "platform/ControlServer.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::string request(const std::filesystem::path& socket_path, const std::string& wire) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1U);
    assert(connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    const std::string line = wire + "\n";
    assert(send(fd, line.data(), line.size(), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(line.size()));

    std::string response;
    char buffer[256];
    while (response.find('\n') == std::string::npos) {
        const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        assert(received > 0);
        response.append(buffer, static_cast<std::size_t>(received));
    }
    close(fd);
    return response;
}

}  // namespace

int main() {
    const auto socket_path = std::filesystem::temp_directory_path() /
                             ("micropanel-touch-control-" + std::to_string(getpid()) + ".sock");
    micropanel_touch::core::UiEventQueue event_queue;
    micropanel_touch::platform::ControlServer server(event_queue);
    std::string diagnostic;
    assert(server.start(socket_path, &diagnostic));

    struct stat socket_stat {};
    assert(stat(socket_path.c_str(), &socket_stat) == 0);
    assert((socket_stat.st_mode & 0777) == 0600);

    auto pending_response = std::async(std::launch::async, request, socket_path,
                                       R"({"id":7,"command":"navigate","target":"network_menu"})");
    std::optional<micropanel_touch::core::UiControlRequest> request_event;
    for (unsigned int attempt = 0U; attempt < 100U && !request_event.has_value(); ++attempt) {
        for (auto& event : event_queue.drain()) {
            if (auto* request = std::get_if<micropanel_touch::core::UiControlRequest>(&event.payload)) {
                request_event = std::move(*request);
                break;
            }
        }
        if (!request_event.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    assert(request_event.has_value());
    assert(request_event->command.type == micropanel_touch::core::UiControlCommandType::Navigate);
    assert(request_event->command.target == "network_menu");
    request_event->completion->set_value({true, "network_menu", {"network_menu"}, {}});

    const nlohmann::json response = nlohmann::json::parse(pending_response.get());
    assert(response.at("id") == 7);
    assert(response.at("ok") == true);
    assert(response.at("screen") == "network_menu");
    assert(response.at("settled") == true);

    const nlohmann::json bad = nlohmann::json::parse(request(socket_path, R"({"command":"tap"})"));
    assert(bad.at("ok") == false);
    assert(bad.at("error") == "unsupported command");

    server.stop();
    assert(!std::filesystem::exists(socket_path));
    return 0;
}
