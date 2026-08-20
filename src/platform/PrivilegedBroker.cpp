#include "platform/PrivilegedBroker.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <nlohmann/json.hpp>
#include <optional>
#include <poll.h>
#include <string_view>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace micropanel_touch::platform {
namespace {

constexpr std::size_t kMaximumRequestBytes = 4096U;
constexpr auto kAcceptPollTimeoutMs = 100;
// This short timeout protects both the server from an idle peer that never
// sends a request and the client from a stalled request write. It must not be
// used for the client's terminal reply: a valid handler can run for 45 s.
constexpr timeval kRequestIoTimeout{5, 0};

timeval to_timeval(std::chrono::seconds timeout) {
    return {static_cast<time_t>(timeout.count()), 0};
}

bool set_diagnostic(std::string* diagnostic, const std::string& message) {
    if (diagnostic != nullptr) {
        *diagnostic = message;
    }
    return false;
}

bool send_all(int client_fd, const std::uint8_t* data, std::size_t count) {
    std::size_t sent = 0U;
    while (sent < count) {
        const ssize_t result = send(client_fd, data + sent, count - sent, MSG_NOSIGNAL);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool send_reply(int client_fd, const core::PrivilegedOperationReply& reply) {
    const nlohmann::json wire{{"ok", reply.ok}, {"message", reply.message}};
    const std::string line = wire.dump() + '\n';
    return send_all(client_fd, reinterpret_cast<const std::uint8_t*>(line.data()), line.size());
}

bool receive_line(int client_fd, std::string* line) {
    line->clear();
    std::array<char, 512> buffer{};
    while (line->size() <= kMaximumRequestBytes) {
        const ssize_t count = recv(client_fd, buffer.data(), buffer.size(), 0);
        if (count <= 0) {
            return false;
        }
        const std::string_view received(buffer.data(), static_cast<std::size_t>(count));
        const std::size_t newline = received.find('\n');
        if (newline != std::string_view::npos) {
            if (newline + 1U != received.size() || line->size() + newline > kMaximumRequestBytes) {
                return false;
            }
            line->append(received.data(), newline);
            return true;
        }
        line->append(received.data(), received.size());
    }
    return false;
}

core::PrivilegedOperationReply error_reply(std::string message) {
    return {false, std::move(message)};
}

bool has_only_static_ipv4_fields(const nlohmann::json& request) {
    constexpr std::array<std::string_view, 5> fields{
        "operation", "interface", "address", "prefix_length", "gateway",
    };
    for (auto item = request.begin(); item != request.end(); ++item) {
        if (std::find(fields.begin(), fields.end(), item.key()) == fields.end()) {
            return false;
        }
    }
    return true;
}

bool has_only_dhcp_fields(const nlohmann::json& request) {
    constexpr std::array<std::string_view, 2> fields{"operation", "interface"};
    for (auto item = request.begin(); item != request.end(); ++item) {
        if (std::find(fields.begin(), fields.end(), item.key()) == fields.end()) {
            return false;
        }
    }
    return true;
}

bool has_only_dhcp_server_fields(const nlohmann::json& request) {
    constexpr std::array<std::string_view, 6> fields{
        "operation", "interface", "address", "prefix_length", "lease_start", "lease_end",
    };
    for (auto item = request.begin(); item != request.end(); ++item) {
        if (std::find(fields.begin(), fields.end(), item.key()) == fields.end()) {
            return false;
        }
    }
    return true;
}

bool has_only_system_update_fields(const nlohmann::json& request) {
    constexpr std::array<std::string_view, 2> fields{"operation", "source"};
    for (auto item = request.begin(); item != request.end(); ++item) {
        if (std::find(fields.begin(), fields.end(), item.key()) == fields.end()) {
            return false;
        }
    }
    return true;
}

std::optional<core::StaticIpv4Operation> parse_static_ipv4(const nlohmann::json& request,
                                                            std::string* diagnostic) {
    if (!request.is_object() || !has_only_static_ipv4_fields(request) ||
        request.value("operation", std::string{}) != "apply_static_ipv4") {
        set_diagnostic(diagnostic, "request is not an allowed privileged operation");
        return std::nullopt;
    }
    constexpr std::array<std::string_view, 4> fields{
        "interface", "address", "prefix_length", "gateway",
    };
    for (const std::string_view field : fields) {
        if (!request.contains(field) || !request.at(field).is_string()) {
            set_diagnostic(diagnostic, "static IPv4 request has invalid fields");
            return std::nullopt;
        }
    }
    core::StaticIpv4Operation operation{
        request.at("interface").get<std::string>(),
        {request.at("address").get<std::string>(), request.at("prefix_length").get<std::string>(),
         request.at("gateway").get<std::string>()},
    };
    const core::StaticIpValidationResult validation = core::validate_static_ipv4_operation(operation);
    if (!validation.valid) {
        set_diagnostic(diagnostic, validation.message);
        return std::nullopt;
    }
    return operation;
}

std::optional<core::DhcpServerOperation> parse_dhcp_server(const nlohmann::json& request,
                                                            std::string* diagnostic) {
    if (!request.is_object() || !has_only_dhcp_server_fields(request) ||
        request.value("operation", std::string{}) != "apply_dhcp_server") {
        set_diagnostic(diagnostic, "request is not an allowed privileged operation");
        return std::nullopt;
    }
    constexpr std::array<std::string_view, 5> fields{
        "interface", "address", "prefix_length", "lease_start", "lease_end",
    };
    for (const std::string_view field : fields) {
        if (!request.contains(field) || !request.at(field).is_string()) {
            set_diagnostic(diagnostic, "DHCP server request has invalid fields");
            return std::nullopt;
        }
    }
    core::DhcpServerOperation operation{
        request.at("interface").get<std::string>(),
        {request.at("address").get<std::string>(), request.at("prefix_length").get<std::string>(),
         request.at("lease_start").get<std::string>(), request.at("lease_end").get<std::string>()},
    };
    const core::StaticIpValidationResult validation = core::validate_dhcp_server_operation(operation);
    if (!validation.valid) {
        set_diagnostic(diagnostic, validation.message);
        return std::nullopt;
    }
    return operation;
}

std::optional<core::PrivilegedOperation> parse_privileged_operation(
    const nlohmann::json& request, std::string* diagnostic) {
    if (!request.is_object()) {
        set_diagnostic(diagnostic, "request is not an allowed privileged operation");
        return std::nullopt;
    }
    const std::string operation_name = request.value("operation", std::string{});
    if (operation_name == "apply_static_ipv4") {
        const auto static_operation = parse_static_ipv4(request, diagnostic);
        if (!static_operation.has_value()) {
            return std::nullopt;
        }
        return core::PrivilegedOperation{std::move(*static_operation)};
    }
    if (operation_name == "apply_dhcp_server") {
        const auto dhcp_server_operation = parse_dhcp_server(request, diagnostic);
        if (!dhcp_server_operation.has_value()) {
            return std::nullopt;
        }
        return core::PrivilegedOperation{std::move(*dhcp_server_operation)};
    }
    if (operation_name == "check_system_update") {
        // Like factory reset, the bare operation name is the whole request.
        if (request.size() != 1U) {
            set_diagnostic(diagnostic, "update check request has invalid fields");
            return std::nullopt;
        }
        return core::PrivilegedOperation{core::CheckSystemUpdateOperation{}};
    }
    if (operation_name == "wifi_join") {
        // Exactly the two fields, and the passphrase may be absent for an open
        // network - but if present it must be a string, not a number that
        // nlohmann would happily stringify into something the operator never
        // typed.
        const bool has_passphrase = request.contains("passphrase");
        if (request.size() != (has_passphrase ? 3U : 2U) || !request.contains("ssid") ||
            !request.at("ssid").is_string() ||
            (has_passphrase && !request.at("passphrase").is_string())) {
            set_diagnostic(diagnostic, "Wi-Fi join request has invalid fields");
            return std::nullopt;
        }
        core::WifiJoinOperation join_operation{
            request.at("ssid").get<std::string>(),
            has_passphrase ? request.at("passphrase").get<std::string>() : std::string{}};
        const core::StaticIpValidationResult validation =
            core::validate_wifi_join_operation(join_operation);
        if (!validation.valid) {
            // The validator is written never to quote the secret, which is what
            // makes it safe to forward its message to the client.
            set_diagnostic(diagnostic, validation.message);
            return std::nullopt;
        }
        return core::PrivilegedOperation{std::move(join_operation)};
    }
    if (operation_name == "wifi_profile") {
        if (request.size() != 2U || !request.contains("action") ||
            !request.at("action").is_string()) {
            set_diagnostic(diagnostic, "Wi-Fi profile request has invalid fields");
            return std::nullopt;
        }
        core::WifiProfileAction action{};
        if (!core::parse_wifi_profile_action(request.at("action").get<std::string>(), &action)) {
            set_diagnostic(diagnostic, "Wi-Fi profile request names an unknown action");
            return std::nullopt;
        }
        return core::PrivilegedOperation{core::WifiProfileOperation{action}};
    }
    if (operation_name == "wifi_forget") {
        if (request.size() != 1U) {
            set_diagnostic(diagnostic, "Wi-Fi forget request has invalid fields");
            return std::nullopt;
        }
        return core::PrivilegedOperation{core::WifiForgetOperation{}};
    }
    if (operation_name == "power") {
        // Exactly two fields, and the second one must name one of the two
        // actions. A request that carries anything else is rejected whole
        // rather than having its extra fields ignored: an ignored field is a
        // field someone thought would do something.
        if (request.size() != 2U || !request.contains("action") ||
            !request.at("action").is_string()) {
            set_diagnostic(diagnostic, "power request has invalid fields");
            return std::nullopt;
        }
        core::PowerAction action{};
        if (!core::parse_power_action(request.at("action").get<std::string>(), &action)) {
            set_diagnostic(diagnostic, "power request names an unknown action");
            return std::nullopt;
        }
        return core::PrivilegedOperation{core::PowerOperation{action}};
    }
    if (operation_name == "factory_reset") {
        // The only valid factory-reset request is the bare operation name.
        if (request.size() != 1U) {
            set_diagnostic(diagnostic, "factory reset request has invalid fields");
            return std::nullopt;
        }
        return core::PrivilegedOperation{core::FactoryResetOperation{}};
    }
    if (operation_name == "apply_system_update") {
        if (!has_only_system_update_fields(request) || !request.contains("source") ||
            !request.at("source").is_string()) {
            set_diagnostic(diagnostic, "system update request has invalid fields");
            return std::nullopt;
        }
        core::SystemUpdateOperation update_operation{request.at("source").get<std::string>()};
        const core::StaticIpValidationResult validation =
            core::validate_system_update_operation(update_operation);
        if (!validation.valid) {
            set_diagnostic(diagnostic, validation.message);
            return std::nullopt;
        }
        return core::PrivilegedOperation{std::move(update_operation)};
    }
    if (operation_name != "apply_dhcp" || !has_only_dhcp_fields(request) ||
        !request.contains("interface") || !request.at("interface").is_string()) {
        set_diagnostic(diagnostic, "request is not an allowed privileged operation");
        return std::nullopt;
    }
    core::DhcpOperation dhcp_operation{request.at("interface").get<std::string>()};
    const core::StaticIpValidationResult validation = core::validate_dhcp_operation(dhcp_operation);
    if (!validation.valid) {
        set_diagnostic(diagnostic, validation.message);
        return std::nullopt;
    }
    return core::PrivilegedOperation{std::move(dhcp_operation)};
}

bool authenticated_as(int client_fd, uid_t allowed_uid) {
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    return getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0 &&
           length == sizeof(credentials) && credentials.uid == allowed_uid;
}

bool connect_socket(const std::filesystem::path& socket_path, int* client_fd,
                    std::string* diagnostic, std::chrono::seconds reply_timeout) {
    if (!socket_path.is_absolute()) {
        return set_diagnostic(diagnostic, "broker socket path must be absolute");
    }
    const std::string native_path = socket_path.string();
    if (native_path.empty() || native_path.size() >= sizeof(sockaddr_un::sun_path)) {
        return set_diagnostic(diagnostic, "broker socket path is too long");
    }
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return set_diagnostic(diagnostic, "unable to create broker client socket");
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, native_path.c_str(), sizeof(address.sun_path) - 1U);
    if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return set_diagnostic(diagnostic, "unable to connect to privileged broker");
    }
    const timeval reply_timeval = to_timeval(reply_timeout);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &reply_timeval, sizeof(reply_timeval));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &kRequestIoTimeout, sizeof(kRequestIoTimeout));
    *client_fd = fd;
    return true;
}

core::PrivilegedOperationReply send_request(const std::filesystem::path& socket_path,
                                            const nlohmann::json& request,
                                            std::string* diagnostic,
                                            std::chrono::seconds reply_timeout =
                                                kBrokerClientReplyTimeout) {
    int client_fd = -1;
    if (!connect_socket(socket_path, &client_fd, diagnostic, reply_timeout)) {
        return error_reply(diagnostic == nullptr ? "unable to connect to privileged broker" : *diagnostic);
    }
    const std::string line = request.dump() + '\n';
    if (!send_all(client_fd, reinterpret_cast<const std::uint8_t*>(line.data()), line.size())) {
        close(client_fd);
        return error_reply("unable to send privileged broker request");
    }
    std::string response_line;
    if (!receive_line(client_fd, &response_line)) {
        close(client_fd);
        return error_reply("invalid privileged broker response");
    }
    close(client_fd);
    try {
        const nlohmann::json response = nlohmann::json::parse(response_line);
        if (!response.is_object() || !response.contains("ok") || !response.at("ok").is_boolean() ||
            !response.contains("message") || !response.at("message").is_string()) {
            return error_reply("invalid privileged broker response");
        }
        return {response.at("ok").get<bool>(), response.at("message").get<std::string>()};
    } catch (const std::exception&) {
        return error_reply("invalid privileged broker response");
    }
}

}  // namespace

PrivilegedBrokerServer::PrivilegedBrokerServer(PrivilegedExecutor privileged_executor)
    : privileged_executor_(std::move(privileged_executor)) {}

PrivilegedBrokerServer::~PrivilegedBrokerServer() {
    stop();
}

bool PrivilegedBrokerServer::start(const std::filesystem::path& socket_path, uid_t allowed_uid,
                                   std::string* diagnostic) {
    if (running_.load()) {
        return set_diagnostic(diagnostic, "privileged broker is already running");
    }
    if (!socket_path.is_absolute() || allowed_uid == static_cast<uid_t>(-1) ||
        !privileged_executor_) {
        return set_diagnostic(diagnostic, "privileged broker has invalid startup parameters");
    }
    const std::string native_path = socket_path.string();
    if (native_path.empty() || native_path.size() >= sizeof(sockaddr_un::sun_path)) {
        return set_diagnostic(diagnostic, "broker socket path is too long");
    }

    struct stat existing {};
    if (lstat(native_path.c_str(), &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode) || unlink(native_path.c_str()) != 0) {
            return set_diagnostic(diagnostic, "refusing to replace broker socket path");
        }
    } else if (errno != ENOENT) {
        return set_diagnostic(diagnostic, "unable to inspect broker socket path");
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return set_diagnostic(diagnostic, "unable to create broker socket");
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, native_path.c_str(), sizeof(address.sun_path) - 1U);
    const mode_t previous_umask = umask(0077);
    const int bind_result = bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    umask(previous_umask);
    if (bind_result != 0) {
        close(fd);
        return set_diagnostic(diagnostic, "unable to bind broker socket");
    }
    if (chown(native_path.c_str(), allowed_uid, static_cast<gid_t>(-1)) != 0 ||
        chmod(native_path.c_str(), S_IRUSR | S_IWUSR) != 0 || listen(fd, 4) != 0) {
        close(fd);
        unlink(native_path.c_str());
        return set_diagnostic(diagnostic, "unable to secure or listen on broker socket");
    }

    socket_path_ = socket_path;
    allowed_uid_ = allowed_uid;
    listen_fd_.store(fd);
    cancellation_requested_.store(false);
    running_.store(true);
    worker_ = std::thread(&PrivilegedBrokerServer::serve, this);
    return true;
}

void PrivilegedBrokerServer::stop() {
    cancellation_requested_.store(true);
    running_.store(false);
    const int listen_fd = listen_fd_.load();
    if (listen_fd >= 0) {
        shutdown(listen_fd, SHUT_RDWR);
    }
    const int client_fd = active_client_fd_.load();
    if (client_fd >= 0) {
        shutdown(client_fd, SHUT_RDWR);
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
    active_client_fd_.store(-1);
}

void PrivilegedBrokerServer::serve() {
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
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &kRequestIoTimeout,
                   sizeof(kRequestIoTimeout));

        if (!authenticated_as(client_fd, allowed_uid_)) {
            send_reply(client_fd, error_reply("broker client is not authorized"));
        } else {
            std::string line;
            if (!receive_line(client_fd, &line)) {
                send_reply(client_fd, error_reply("invalid or oversized broker request"));
            } else {
                try {
                    const nlohmann::json request = nlohmann::json::parse(line);
                    std::string diagnostic;
                    const auto operation = parse_privileged_operation(request, &diagnostic);
                    send_reply(client_fd, operation.has_value()
                                              ? privileged_executor_(*operation, cancellation_requested_)
                                              : error_reply(std::move(diagnostic)));
                } catch (const std::exception&) {
                    send_reply(client_fd, error_reply("invalid JSON broker request"));
                }
            }
        }
        active_client_fd_.store(-1);
        close(client_fd);
    }
}

core::PrivilegedOperationReply PrivilegedBrokerClient::apply_static_ipv4(
    const std::filesystem::path& socket_path, const core::StaticIpv4Operation& operation,
    std::string* diagnostic) {
    const core::StaticIpValidationResult validation = core::validate_static_ipv4_operation(operation);
    if (!validation.valid) {
        return error_reply(validation.message);
    }
    const nlohmann::json request{
        {"operation", "apply_static_ipv4"},
        {"interface", operation.interface_name},
        {"address", operation.settings.address},
        {"prefix_length", operation.settings.prefix_length},
        {"gateway", operation.settings.gateway},
    };
    return send_request(socket_path, request, diagnostic);
}

core::PrivilegedOperationReply PrivilegedBrokerClient::apply_dhcp(
    const std::filesystem::path& socket_path, const core::DhcpOperation& operation,
    std::string* diagnostic) {
    const core::StaticIpValidationResult validation = core::validate_dhcp_operation(operation);
    if (!validation.valid) {
        return error_reply(validation.message);
    }
    return send_request(socket_path,
                        nlohmann::json{{"operation", "apply_dhcp"},
                                       {"interface", operation.interface_name}},
                        diagnostic);
}

core::PrivilegedOperationReply PrivilegedBrokerClient::apply_dhcp_server(
    const std::filesystem::path& socket_path, const core::DhcpServerOperation& operation,
    std::string* diagnostic) {
    const core::StaticIpValidationResult validation = core::validate_dhcp_server_operation(operation);
    if (!validation.valid) {
        return error_reply(validation.message);
    }
    return send_request(socket_path,
                        nlohmann::json{{"operation", "apply_dhcp_server"},
                                       {"interface", operation.interface_name},
                                       {"address", operation.settings.address},
                                       {"prefix_length", operation.settings.prefix_length},
                                       {"lease_start", operation.settings.lease_start},
                                       {"lease_end", operation.settings.lease_end}},
                        diagnostic);
}

core::PrivilegedOperationReply PrivilegedBrokerClient::factory_reset(
    const std::filesystem::path& socket_path, std::string* diagnostic) {
    return send_request(socket_path, nlohmann::json{{"operation", "factory_reset"}}, diagnostic,
                        kSystemUpdateClientReplyTimeout);
}

core::PrivilegedOperationReply PrivilegedBrokerClient::wifi_join(
    const std::filesystem::path& socket_path, const core::WifiJoinOperation& operation,
    std::string* diagnostic) {
    const core::StaticIpValidationResult validation =
        core::validate_wifi_join_operation(operation);
    if (!validation.valid) {
        return error_reply(validation.message);
    }
    nlohmann::json request{{"operation", "wifi_join"}, {"ssid", operation.ssid}};
    if (!operation.passphrase.empty()) {
        request["passphrase"] = operation.passphrase;
    }
    // The association itself is what takes the time here: a wrong password is
    // reported by the AP, not guessed locally, and that round trip is slower
    // than a network reconfiguration.
    return send_request(socket_path, request, diagnostic, kSystemUpdateClientReplyTimeout);
}

core::PrivilegedOperationReply PrivilegedBrokerClient::wifi_forget(
    const std::filesystem::path& socket_path, std::string* diagnostic) {
    return send_request(socket_path, nlohmann::json{{"operation", "wifi_forget"}}, diagnostic);
}

core::PrivilegedOperationReply PrivilegedBrokerClient::wifi_profile(
    const std::filesystem::path& socket_path, const core::WifiProfileOperation& operation,
    std::string* diagnostic) {
    return send_request(
        socket_path,
        nlohmann::json{{"operation", "wifi_profile"},
                       {"action", std::string(core::wifi_profile_action_name(operation.action))}},
        diagnostic, kSystemUpdateClientReplyTimeout);
}

core::PrivilegedOperationReply PrivilegedBrokerClient::power(
    const std::filesystem::path& socket_path, const core::PowerOperation& operation,
    std::string* diagnostic) {
    return send_request(
        socket_path,
        nlohmann::json{{"operation", "power"},
                       {"action", std::string(core::power_action_name(operation.action))}},
        diagnostic);
}

core::PrivilegedOperationReply PrivilegedBrokerClient::check_system_update(
    const std::filesystem::path& socket_path, std::string* diagnostic) {
    return send_request(socket_path, nlohmann::json{{"operation", "check_system_update"}},
                        diagnostic, kSystemUpdateClientReplyTimeout);
}

core::PrivilegedOperationReply PrivilegedBrokerClient::apply_system_update(
    const std::filesystem::path& socket_path, const core::SystemUpdateOperation& operation,
    std::string* diagnostic) {
    const core::StaticIpValidationResult validation = core::validate_system_update_operation(operation);
    if (!validation.valid) {
        return error_reply(validation.message);
    }
    return send_request(socket_path,
                        nlohmann::json{{"operation", "apply_system_update"},
                                       {"source", operation.source}},
                        diagnostic, kSystemUpdateClientReplyTimeout);
}

}  // namespace micropanel_touch::platform
