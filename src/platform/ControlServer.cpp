#include "platform/ControlServer.h"

#include "core/UiControl.h"
#include "core/UiEventQueue.h"

#include <array>
#include <cerrno>
#include <chrono>
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
constexpr auto kUiReplyTimeout = std::chrono::seconds(2);
constexpr auto kAcceptPollTimeoutMs = 100;

bool set_diagnostic(std::string* diagnostic, const std::string& message) {
    if (diagnostic != nullptr) {
        *diagnostic = message;
    }
    return false;
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

void send_frame_response(int client_fd, nlohmann::json response, const Rgb565Frame& frame) {
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

ControlServer::ControlServer(core::UiEventQueue& event_queue, FrameCaptureProvider frame_capture)
    : event_queue_(event_queue), frame_capture_(std::move(frame_capture)) {}

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
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return set_diagnostic(diagnostic, "unable to bind control socket");
    }
    if (chmod(native_path.c_str(), S_IRUSR | S_IWUSR) != 0 || listen(fd, 4) != 0) {
        close(fd);
        unlink(native_path.c_str());
        return set_diagnostic(diagnostic, "unable to secure or listen on control socket");
    }

    socket_path_ = socket_path;
    listen_fd_ = fd;
    running_.store(true);
    worker_ = std::thread(&ControlServer::serve, this);
    return true;
}

void ControlServer::stop() {
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

void ControlServer::serve() {
    while (running_.load()) {
        pollfd descriptor{listen_fd_, POLLIN, 0};
        const int ready = poll(&descriptor, 1, kAcceptPollTimeoutMs);
        if (ready <= 0 || (descriptor.revents & POLLIN) == 0) {
            continue;
        }
        const int client_fd = accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client_fd < 0) {
            continue;
        }
        const timeval timeout{2, 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        std::string line;
        nlohmann::json request_id = nullptr;
        if (!receive_line(client_fd, &line)) {
            send_response(client_fd, make_error(request_id, "invalid or oversized request"));
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
                close(client_fd);
                continue;
            }

            const bool capture_frame = command.type == core::UiControlCommandType::CaptureFrame;
            auto completion = std::make_shared<std::promise<core::UiControlResponse>>();
            std::future<core::UiControlResponse> reply = completion->get_future();
            event_queue_.push({next_sequence_.fetch_add(1U),
                               core::UiControlRequest{std::move(command), std::move(completion)}});
            if (reply.wait_for(kUiReplyTimeout) != std::future_status::ready) {
                send_response(client_fd, make_error(request_id, "UI response timed out"));
            } else {
                const core::UiControlResponse response = reply.get();
                if (capture_frame && response.ok) {
                    std::string frame_diagnostic;
                    const auto frame = frame_capture_ ? frame_capture_(&frame_diagnostic) : std::nullopt;
                    if (!frame.has_value()) {
                        if (frame_diagnostic.empty()) {
                            frame_diagnostic = "no framebuffer capture provider is configured";
                        }
                        send_response(client_fd, make_error(request_id,
                                                            "frame capture failed: " + frame_diagnostic));
                    } else {
                        send_frame_response(client_fd, make_response(request_id, response), *frame);
                    }
                } else {
                    send_response(client_fd, make_response(request_id, response));
                }
            }
        } catch (const std::exception&) {
            send_response(client_fd, make_error(request_id, "invalid JSON request"));
        }
        close(client_fd);
    }
}

}  // namespace micropanel_touch::platform
