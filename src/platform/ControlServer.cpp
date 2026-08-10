#include "platform/ControlServer.h"

#include "core/UiControl.h"
#include "core/UiEventQueue.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <future>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace micropanel_touch::platform {
namespace {

constexpr std::size_t kMaximumRequestBytes = 4096U;
constexpr std::size_t kMaximumTextInputBytes = 63U;
constexpr auto kUiReplyTimeout = std::chrono::seconds(2);
constexpr auto kAcceptPollTimeoutMs = 100;

bool set_diagnostic(std::string* diagnostic, const std::string& message) {
    if (diagnostic != nullptr) {
        *diagnostic = message;
    }
    return false;
}

bool is_printable_ascii(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char character) {
        return character >= 0x20U && character <= 0x7eU;
    });
}

bool parse_command(const nlohmann::json& request, core::UiControlCommand* command,
                   std::string* diagnostic) {
    if (!request.is_object() || !request.contains("command") ||
        !request.at("command").is_string()) {
        return set_diagnostic(diagnostic, "request requires string command");
    }
    const std::string name = request.at("command").get<std::string>();
    if (name == "state") {
        command->type = core::UiControlCommandType::State;
        command->target.clear();
        return true;
    }
    if (name == "back") {
        command->type = core::UiControlCommandType::Back;
        command->target.clear();
        return true;
    }
    if (name == "capture_tree") {
        command->type = core::UiControlCommandType::CaptureTree;
        command->target.clear();
        return true;
    }
    if (name == "capture_frame") {
        command->type = core::UiControlCommandType::CaptureFrame;
        command->target.clear();
        return true;
    }
    if (name == "tap") {
        if (!request.contains("x") || !request.contains("y") || !request.at("x").is_number_integer() ||
            !request.at("y").is_number_integer()) {
            return set_diagnostic(diagnostic, "tap requires integer x and y");
        }
        const std::int64_t x = request.at("x").get<std::int64_t>();
        const std::int64_t y = request.at("y").get<std::int64_t>();
        if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) {
            return set_diagnostic(diagnostic, "tap coordinates are outside the integer range");
        }
        command->type = core::UiControlCommandType::Tap;
        command->target.clear();
        command->x = static_cast<std::int32_t>(x);
        command->y = static_cast<std::int32_t>(y);
        return true;
    }
    if (name == "text") {
        if (!request.contains("field") || !request.at("field").is_string() ||
            !request.contains("text") || !request.at("text").is_string()) {
            return set_diagnostic(diagnostic, "text requires string field and text");
        }
        command->target = request.at("field").get<std::string>();
        command->text = request.at("text").get<std::string>();
        if (command->target.empty() || command->target.size() > 32U) {
            return set_diagnostic(diagnostic, "text field must contain 1..32 bytes");
        }
        if (command->text.empty() || command->text.size() > kMaximumTextInputBytes ||
            !is_printable_ascii(command->text)) {
            return set_diagnostic(diagnostic, "text must contain 1..63 printable ASCII bytes");
        }
        command->type = core::UiControlCommandType::Text;
        return true;
    }
    if (name != "navigate" && name != "activate") {
        return set_diagnostic(diagnostic, "unsupported command");
    }
    if (!request.contains("target") || !request.at("target").is_string()) {
        return set_diagnostic(diagnostic, name + " requires string target");
    }
    command->target = request.at("target").get<std::string>();
    if (command->target.empty() || command->target.size() > 256U) {
        return set_diagnostic(diagnostic, "target must contain 1..256 bytes");
    }
    command->type = name == "navigate" ? core::UiControlCommandType::Navigate
                                          : core::UiControlCommandType::Activate;
    return true;
}

bool receive_line(int client_fd, std::string* line) {
    std::array<char, 512> buffer{};
    while (line->size() <= kMaximumRequestBytes) {
        const ssize_t received = recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            return false;
        }
        const std::string_view chunk(buffer.data(), static_cast<std::size_t>(received));
        const std::size_t newline = chunk.find('\n');
        line->append(chunk.substr(0U, newline));
        if (newline != std::string_view::npos) {
            return line->size() <= kMaximumRequestBytes;
        }
    }
    return false;
}

bool send_all(int client_fd, const std::uint8_t* bytes, std::size_t length) {
    std::size_t sent = 0U;
    while (sent < length) {
        const ssize_t result = send(client_fd, bytes + sent, length - sent, MSG_NOSIGNAL);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

void send_response(int client_fd, const nlohmann::json& response) {
    const std::string wire = response.dump() + "\n";
    send_all(client_fd, reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size());
}

void send_frame_response(int client_fd, nlohmann::json response, const core::UiFrameCapture& frame) {
    response["capture"] = {
        {"format", "rgb565le"},
        {"width", frame.width},
        {"height", frame.height},
        {"stride_bytes", frame.stride_bytes},
        {"byte_count", frame.pixels.size()},
    };
    const std::string header = response.dump() + "\n";
    if (!send_all(client_fd, reinterpret_cast<const std::uint8_t*>(header.data()), header.size())) {
        return;
    }
    send_all(client_fd, frame.pixels.data(), frame.pixels.size());
}

nlohmann::json make_error(const nlohmann::json& request_id, const std::string& error) {
    return {{"id", request_id}, {"ok", false}, {"error", error}};
}

nlohmann::json make_response(const nlohmann::json& request_id,
                             const core::UiControlResponse& response) {
    nlohmann::json wire{{"id", request_id}, {"ok", response.ok}};
    if (response.ok) {
        // "settled" means the LVGL thread completed layout/refresh into
        // framebuffer memory. DRM/fbdev may still flush that memory to SPI
        // asynchronously, so it is not a promise that photons are emitted.
        wire["screen"] = response.screen_id;
        wire["menu_path"] = response.menu_path;
        wire["settled"] = true;
        if (!response.widgets.empty() || response.widget_tree_truncated) {
            wire["widget_tree_truncated"] = response.widget_tree_truncated;
            wire["widgets"] = nlohmann::json::array();
            for (const auto& widget : response.widgets) {
                wire["widgets"].push_back({
                    {"id", widget.id},
                    {"parent_id", widget.parent_id},
                    {"type", widget.type},
                    {"text", widget.text},
                    {"x", widget.x},
                    {"y", widget.y},
                    {"width", widget.width},
                    {"height", widget.height},
                    {"redacted", widget.redacted},
                    {"text_truncated", widget.text_truncated},
                });
            }
        }
    } else {
        wire["error"] = response.error;
    }
    return wire;
}

}  // namespace

ControlServer::ControlServer(core::UiEventQueue& event_queue) : event_queue_(event_queue) {}

ControlServer::~ControlServer() {
    stop();
}

bool ControlServer::start(const std::filesystem::path& socket_path, std::string* diagnostic) {
    if (running_.load()) {
        return set_diagnostic(diagnostic, "control server is already running");
    }
    if (!socket_path.is_absolute()) {
        return set_diagnostic(diagnostic, "control socket path must be absolute");
    }
    const std::string native_path = socket_path.string();
    if (native_path.empty() || native_path.size() >= sizeof(sockaddr_un::sun_path)) {
        return set_diagnostic(diagnostic, "control socket path is too long");
    }

    struct stat existing {};
    if (lstat(native_path.c_str(), &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode)) {
            return set_diagnostic(diagnostic, "refusing to replace non-socket control path");
        }
        if (unlink(native_path.c_str()) != 0) {
            return set_diagnostic(diagnostic, "unable to remove stale control socket");
        }
    } else if (errno != ENOENT) {
        return set_diagnostic(diagnostic, "unable to inspect control socket path");
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return set_diagnostic(diagnostic, "unable to create control socket");
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, native_path.c_str(), sizeof(address.sun_path) - 1U);
    // bind() applies the process umask to the filesystem node. Use a private
    // mask for the small bind-to-chmod window rather than relying on a caller's
    // umask to avoid exposing a just-created development socket.
    const mode_t previous_umask = umask(0077);
    const int bind_result = bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    umask(previous_umask);
    if (bind_result != 0) {
        close(fd);
        return set_diagnostic(diagnostic, "unable to bind control socket");
    }
    if (chmod(native_path.c_str(), S_IRUSR | S_IWUSR) != 0 || listen(fd, 4) != 0) {
        close(fd);
        unlink(native_path.c_str());
        return set_diagnostic(diagnostic, "unable to secure or listen on control socket");
    }

    socket_path_ = socket_path;
    listen_fd_.store(fd);
    running_.store(true);
    worker_ = std::thread(&ControlServer::serve, this);
    return true;
}

void ControlServer::stop() {
    running_.store(false);
    const int listen_fd = listen_fd_.load();
    if (listen_fd >= 0) {
        shutdown(listen_fd, SHUT_RDWR);
    }
    const int active_client_fd = active_client_fd_.load();
    if (active_client_fd >= 0) {
        shutdown(active_client_fd, SHUT_RDWR);
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    const int owned_listen_fd = listen_fd_.exchange(-1);
    if (owned_listen_fd >= 0) {
        close(owned_listen_fd);
    }
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

void ControlServer::serve() {
    while (running_.load()) {
        const int listen_fd = listen_fd_.load();
        if (listen_fd < 0) {
            return;
        }
        pollfd descriptor{listen_fd, POLLIN, 0};
        const int ready = poll(&descriptor, 1, kAcceptPollTimeoutMs);
        if (!running_.load() || ready <= 0 || (descriptor.revents & POLLIN) == 0) {
            continue;
        }
        const int client_fd = accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client_fd < 0) {
            continue;
        }
        active_client_fd_.store(client_fd);
        const timeval timeout{2, 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        std::string line;
        nlohmann::json request_id = nullptr;
        if (!receive_line(client_fd, &line)) {
            send_response(client_fd, make_error(request_id, "invalid or oversized request"));
            active_client_fd_.store(-1);
            close(client_fd);
            continue;
        }
        try {
            const nlohmann::json request = nlohmann::json::parse(line);
            if (request.contains("id")) {
                request_id = request.at("id");
            }
            core::UiControlCommand command;
            std::string diagnostic;
            if (!parse_command(request, &command, &diagnostic)) {
                send_response(client_fd, make_error(request_id, diagnostic));
                active_client_fd_.store(-1);
                close(client_fd);
                continue;
            }

            const bool capture_frame = command.type == core::UiControlCommandType::CaptureFrame;
            auto completion = std::make_shared<std::promise<core::UiControlResponse>>();
            std::future<core::UiControlResponse> reply = completion->get_future();
            event_queue_.push({next_sequence_.fetch_add(1U),
                               core::UiControlRequest{std::move(command), std::move(completion)}});
            const auto deadline = std::chrono::steady_clock::now() + kUiReplyTimeout;
            while (running_.load() && reply.wait_for(std::chrono::milliseconds(25)) !=
                                          std::future_status::ready &&
                   std::chrono::steady_clock::now() < deadline) {
            }
            if (!running_.load()) {
                // stop() already shut down this client; avoid a stale reply.
            } else if (reply.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                send_response(client_fd, make_error(request_id, "UI response timed out"));
            } else {
                const core::UiControlResponse response = reply.get();
                if (capture_frame && response.ok) {
                    if (!response.frame_capture.has_value()) {
                        send_response(client_fd, make_error(request_id,
                                                            "UI did not provide a settled frame capture"));
                    } else {
                        send_frame_response(client_fd, make_response(request_id, response),
                                            *response.frame_capture);
                    }
                } else {
                    send_response(client_fd, make_response(request_id, response));
                }
            }
        } catch (const std::exception&) {
            send_response(client_fd, make_error(request_id, "invalid JSON request"));
        }
        active_client_fd_.store(-1);
        close(client_fd);
    }
}

}  // namespace micropanel_touch::platform
