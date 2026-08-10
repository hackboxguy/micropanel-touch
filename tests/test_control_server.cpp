#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/UiControl.h"
#include "core/UiEventQueue.h"
#include "platform/ControlServer.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <future>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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

struct FrameResponse {
    nlohmann::json header;
    std::vector<std::uint8_t> pixels;
};

FrameResponse request_frame(const std::filesystem::path& socket_path, const std::string& wire) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1U);
    assert(connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    const std::string line = wire + "\n";
    assert(send(fd, line.data(), line.size(), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(line.size()));

    std::string header_text;
    std::vector<std::uint8_t> pixels;
    char buffer[256];
    bool header_complete = false;
    while (!header_complete) {
        const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        assert(received > 0);
        const std::string_view chunk(buffer, static_cast<std::size_t>(received));
        const std::size_t newline = chunk.find('\n');
        header_text.append(chunk.substr(0U, newline));
        if (newline != std::string_view::npos) {
            pixels.insert(pixels.end(), reinterpret_cast<const std::uint8_t*>(chunk.data() + newline + 1U),
                          reinterpret_cast<const std::uint8_t*>(chunk.data() + chunk.size()));
            header_complete = true;
        }
    }
    const nlohmann::json header = nlohmann::json::parse(header_text);
    const std::size_t expected = header.at("capture").at("byte_count").get<std::size_t>();
    while (pixels.size() < expected) {
        const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        assert(received > 0);
        pixels.insert(pixels.end(), reinterpret_cast<const std::uint8_t*>(buffer),
                      reinterpret_cast<const std::uint8_t*>(buffer + received));
    }
    assert(pixels.size() == expected);
    close(fd);
    return {header, std::move(pixels)};
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
    request_event->completion->set_value(
        {true, "network_menu", {"network_menu"},
         {{0U, -1, "screen", {}, 0, 0, 320, 480, false, false},
          {1U, 0, "textarea", "<redacted>", 16, 60, 288, 44, true, false}},
         false, {}});

    const nlohmann::json response = nlohmann::json::parse(pending_response.get());
    assert(response.at("id") == 7);
    assert(response.at("ok") == true);
    assert(response.at("screen") == "network_menu");
    assert(response.at("settled") == true);
    assert(response.at("widgets").size() == 2U);
    assert(response.at("widgets").at(1).at("redacted") == true);
    assert(response.at("widgets").at(1).at("text") == "<redacted>");

    auto capture_response = std::async(std::launch::async, request, socket_path,
                                       R"({"id":"tree","command":"capture_tree"})");
    std::optional<micropanel_touch::core::UiControlRequest> capture_event;
    for (unsigned int attempt = 0U; attempt < 100U && !capture_event.has_value(); ++attempt) {
        for (auto& event : event_queue.drain()) {
            if (auto* request = std::get_if<micropanel_touch::core::UiControlRequest>(&event.payload)) {
                capture_event = std::move(*request);
                break;
            }
        }
        if (!capture_event.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    assert(capture_event.has_value());
    assert(capture_event->command.type == micropanel_touch::core::UiControlCommandType::CaptureTree);
    capture_event->completion->set_value({true, "root", {}, {}, false, {}});
    const nlohmann::json captured = nlohmann::json::parse(capture_response.get());
    assert(captured.at("id") == "tree");
    assert(captured.at("ok") == true);
    assert(captured.at("settled") == true);

    auto frame_response = std::async(std::launch::async, request_frame, socket_path,
                                     R"({"id":"frame","command":"capture_frame"})");
    std::optional<micropanel_touch::core::UiControlRequest> frame_event;
    for (unsigned int attempt = 0U; attempt < 100U && !frame_event.has_value(); ++attempt) {
        for (auto& event : event_queue.drain()) {
            if (auto* request = std::get_if<micropanel_touch::core::UiControlRequest>(&event.payload)) {
                frame_event = std::move(*request);
                break;
            }
        }
        if (!frame_event.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    assert(frame_event.has_value());
    assert(frame_event->command.type == micropanel_touch::core::UiControlCommandType::CaptureFrame);
    micropanel_touch::core::UiControlResponse frame_reply{true, "root", {}, {}, false, {}};
    frame_reply.frame_capture = micropanel_touch::core::UiFrameCapture{
        2U, 2U, 4U, {0x00U, 0xf8U, 0xe0U, 0x07U, 0x1fU, 0x00U, 0xffU, 0xffU}};
    frame_event->completion->set_value(std::move(frame_reply));
    const FrameResponse frame = frame_response.get();
    assert(frame.header.at("id") == "frame");
    assert(frame.header.at("capture").at("format") == "rgb565le");
    assert(frame.header.at("capture").at("width") == 2U);
    assert(frame.header.at("capture").at("height") == 2U);
    assert(frame.header.at("capture").at("stride_bytes") == 4U);
    assert((frame.pixels == std::vector<std::uint8_t>{0x00U, 0xf8U, 0xe0U, 0x07U,
                                                        0x1fU, 0x00U, 0xffU, 0xffU}));

    auto tap_response = std::async(std::launch::async, request, socket_path,
                                   R"({"id":"tap","command":"tap","x":160,"y":76})");
    std::optional<micropanel_touch::core::UiControlRequest> tap_event;
    for (unsigned int attempt = 0U; attempt < 100U && !tap_event.has_value(); ++attempt) {
        for (auto& event : event_queue.drain()) {
            if (auto* request = std::get_if<micropanel_touch::core::UiControlRequest>(&event.payload)) {
                tap_event = std::move(*request);
                break;
            }
        }
        if (!tap_event.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    assert(tap_event.has_value());
    assert(tap_event->command.type == micropanel_touch::core::UiControlCommandType::Tap);
    assert(tap_event->command.x == 160);
    assert(tap_event->command.y == 76);
    tap_event->completion->set_value({true, "network_menu", {"network_menu"}, {}, false, {}});
    const nlohmann::json tapped = nlohmann::json::parse(tap_response.get());
    assert(tapped.at("id") == "tap");
    assert(tapped.at("ok") == true);
    assert(tapped.at("screen") == "network_menu");
    assert(tapped.at("settled") == true);

    auto text_response = std::async(std::launch::async, request, socket_path,
                                    R"({"id":"text","command":"text","field":"ip_address","text":"10.0.0.2"})");
    std::optional<micropanel_touch::core::UiControlRequest> text_event;
    for (unsigned int attempt = 0U; attempt < 100U && !text_event.has_value(); ++attempt) {
        for (auto& event : event_queue.drain()) {
            if (auto* request = std::get_if<micropanel_touch::core::UiControlRequest>(&event.payload)) {
                text_event = std::move(*request);
                break;
            }
        }
        if (!text_event.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    assert(text_event.has_value());
    assert(text_event->command.type == micropanel_touch::core::UiControlCommandType::Text);
    assert(text_event->command.target == "ip_address");
    assert(text_event->command.text == "10.0.0.2");
    text_event->completion->set_value({true, "netsettings", {"network", "netsettings"}, {}, false, {}});
    const nlohmann::json typed = nlohmann::json::parse(text_response.get());
    assert(typed.at("id") == "text");
    assert(typed.at("ok") == true);
    assert(typed.at("screen") == "netsettings");
    assert(!typed.contains("text"));

    const nlohmann::json bad = nlohmann::json::parse(request(socket_path, R"({"command":"tap"})"));
    assert(bad.at("ok") == false);
    assert(bad.at("error") == "tap requires integer x and y");

    const nlohmann::json fractional =
        nlohmann::json::parse(request(socket_path, R"({"command":"tap","x":1.5,"y":2})"));
    assert(fractional.at("ok") == false);
    assert(fractional.at("error") == "tap requires integer x and y");

    const nlohmann::json missing_text_field =
        nlohmann::json::parse(request(socket_path, R"({"command":"text","text":"10"})"));
    assert(missing_text_field.at("ok") == false);
    assert(missing_text_field.at("error") == "text requires string field and text");

    // A connected client that never sends its newline used to hold stop()
    // behind the receive timeout. stop() shuts down the active descriptor so
    // the worker releases it promptly.
    const int idle_client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(idle_client >= 0);
    sockaddr_un idle_address{};
    idle_address.sun_family = AF_UNIX;
    std::strncpy(idle_address.sun_path, socket_path.c_str(), sizeof(idle_address.sun_path) - 1U);
    assert(connect(idle_client, reinterpret_cast<const sockaddr*>(&idle_address), sizeof(idle_address)) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto stopped = std::async(std::launch::async, [&server] { server.stop(); });
    assert(stopped.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
    stopped.get();
    close(idle_client);
    assert(!std::filesystem::exists(socket_path));
    return 0;
}
