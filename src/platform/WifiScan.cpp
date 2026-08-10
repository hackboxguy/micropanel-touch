#include "platform/WifiScan.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace micropanel_touch::platform {
namespace {

constexpr std::size_t kMaximumCommandOutputBytes = 64U * 1024U;

struct CommandResult {
    int exit_status{EXIT_FAILURE};
    std::string output;
};

std::vector<std::string> split_escaped_fields(std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    bool escaped = false;
    for (const char character : line) {
        if (escaped) {
            field += character;
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == ':') {
            fields.push_back(std::move(field));
            field.clear();
        } else {
            field += character;
        }
    }
    if (escaped) {
        field += '\\';
    }
    fields.push_back(std::move(field));
    return fields;
}

unsigned int parse_signal_percent(const std::string& value) {
    try {
        const unsigned long parsed = std::stoul(value);
        return static_cast<unsigned int>(std::min(parsed, 100UL));
    } catch (const std::exception&) {
        return 0;
    }
}

CommandResult run_nmcli(const std::vector<const char*>& arguments) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return {EXIT_FAILURE, "Cannot create NetworkManager output pipe."};
    }

    // Build argv before fork: this worker process has other threads, so the
    // child must not allocate before exec.
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2U);
    argv.push_back(const_cast<char*>("nmcli"));
    for (const char* const argument : arguments) {
        argv.push_back(const_cast<char*>(argument));
    }
    argv.push_back(nullptr);

    const pid_t child = fork();
    if (child == -1) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return {EXIT_FAILURE, "Cannot start NetworkManager scan."};
    }
    if (child == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execv("/usr/bin/nmcli", argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    CommandResult result;
    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t count = read(pipe_fds[0], buffer.data(), buffer.size());
        if (count > 0) {
            const std::size_t remaining = kMaximumCommandOutputBytes - result.output.size();
            result.output.append(buffer.data(), std::min<std::size_t>(remaining, count));
            if (result.output.size() == kMaximumCommandOutputBytes) {
                break;
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    close(pipe_fds[0]);

    int status = 0;
    while (waitpid(child, &status, 0) == -1 && errno == EINTR) {
    }
    result.exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
    return result;
}

CommandResult run_nmcli_wifi_scan() {
    return run_nmcli({"--terse", "--escape", "yes", "--fields",
                      "IN-USE,SSID,BSSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan",
                      "yes"});
}

std::string wifi_device_diagnostic() {
    const CommandResult command = run_nmcli(
        {"--terse", "--escape", "yes", "--fields", "DEVICE,TYPE,STATE", "device", "status"});
    if (command.exit_status != EXIT_SUCCESS) {
        return {};
    }

    std::size_t line_start = 0;
    while (line_start < command.output.size()) {
        const std::size_t line_end = command.output.find('\n', line_start);
        const std::string_view line(command.output.data() + line_start,
                                    (line_end == std::string::npos ? command.output.size() : line_end) - line_start);
        const auto fields = split_escaped_fields(line);
        if (fields.size() == 3U && fields[1] == "wifi") {
            if (fields[2] == "unavailable") {
                return "Wi-Fi radio is unavailable on " + fields[0] + ".";
            }
            if (fields[2] == "unmanaged") {
                return "Wi-Fi is unmanaged on " + fields[0] + ".";
            }
        }
        if (line_end == std::string::npos) {
            break;
        }
        line_start = line_end + 1;
    }
    return {};
}

}  // namespace

std::vector<core::WifiAccessPoint> parse_nmcli_wifi_list(const std::string& output) {
    std::vector<core::WifiAccessPoint> access_points;
    std::size_t line_start = 0;
    while (line_start < output.size()) {
        const std::size_t line_end = output.find('\n', line_start);
        const std::string_view line(output.data() + line_start,
                                    (line_end == std::string::npos ? output.size() : line_end) - line_start);
        const auto fields = split_escaped_fields(line);
        if (fields.size() == 5U && !fields[2].empty()) {
            access_points.push_back({fields[0] == "*", fields[1], fields[2],
                                     parse_signal_percent(fields[3]), fields[4]});
        }
        if (line_end == std::string::npos) {
            break;
        }
        line_start = line_end + 1;
    }
    return access_points;
}

WifiScanProvider::WifiScanProvider(core::UiEventQueue& event_queue) : event_queue_(event_queue) {}

WifiScanProvider::~WifiScanProvider() {
    stop();
}

void WifiScanProvider::request_scan() {
    if (running_.exchange(true)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread(&WifiScanProvider::run, this);
}

void WifiScanProvider::stop() {
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void WifiScanProvider::run() {
    const CommandResult command = run_nmcli_wifi_scan();
    core::WifiScanResult result;
    if (command.exit_status == EXIT_SUCCESS) {
        result.access_points = parse_nmcli_wifi_list(command.output);
        if (result.access_points.empty()) {
            result.diagnostic = wifi_device_diagnostic();
            if (result.diagnostic.empty()) {
                result.diagnostic = "No Wi-Fi networks found.";
            }
        }
    } else {
        result.diagnostic = command.output.empty() ? "Wi-Fi scan failed." : command.output;
    }
    event_queue_.push_latest({next_sequence_++, std::move(result)});
    running_.store(false);
}

}  // namespace micropanel_touch::platform
