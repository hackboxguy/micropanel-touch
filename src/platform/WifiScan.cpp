#include "platform/WifiScan.h"

#include "platform/CommandRunner.h"

#include "core/PrivilegedOperations.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace micropanel_touch::platform {
namespace {

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

CommandResult run_nmcli(const std::vector<std::string>& arguments,
                        const std::atomic_bool& cancellation_requested) {
    return CommandRunner::run({"/usr/bin/nmcli", arguments, std::chrono::seconds(15), 64U * 1024U},
                              cancellation_requested);
}

CommandResult run_nmcli_wifi_scan(const std::atomic_bool& cancellation_requested) {
    return run_nmcli({"--terse", "--escape", "yes", "--fields",
                      "IN-USE,SSID,BSSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan",
                      "yes"}, cancellation_requested);
}

// The SSID of the single saved profile. Deliberately a separate, non-secret
// query: the HMI account can read a connection's SSID but not its psk, which
// is the boundary working - the panel learns what it needs to stop asking for
// a password it already has, and learns nothing it should not.
std::string saved_profile_ssid(const std::atomic_bool& cancellation_requested) {
    const CommandResult command = run_nmcli(
        {"--terse", "--escape", "yes", "--get-values", "802-11-wireless.ssid",
         "connection", "show", std::string(core::kWifiProfileId)},
        cancellation_requested);
    if (command.status != CommandStatus::succeeded) {
        return {};
    }
    std::string ssid = command.output;
    while (!ssid.empty() && (ssid.back() == '\n' || ssid.back() == '\r')) {
        ssid.pop_back();
    }
    return ssid;
}

std::string wifi_device_diagnostic(const std::atomic_bool& cancellation_requested) {
    const CommandResult command = run_nmcli(
        {"--terse", "--escape", "yes", "--fields", "DEVICE,TYPE,STATE", "device", "status"},
        cancellation_requested);
    if (command.status != CommandStatus::succeeded) {
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
    cancellation_requested_.store(false);
    worker_ = std::thread(&WifiScanProvider::run, this);
}

void WifiScanProvider::stop() {
    cancellation_requested_.store(true);
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void WifiScanProvider::run() {
    const CommandResult command = run_nmcli_wifi_scan(cancellation_requested_);
    if (command.status == CommandStatus::cancelled || cancellation_requested_.load()) {
        running_.store(false);
        return;
    }
    core::WifiScanResult result;
    result.saved_ssid = saved_profile_ssid(cancellation_requested_);
    if (command.status == CommandStatus::succeeded) {
        result.access_points = parse_nmcli_wifi_list(command.output);
        if (result.access_points.empty()) {
            result.diagnostic = wifi_device_diagnostic(cancellation_requested_);
            if (result.diagnostic.empty()) {
                result.diagnostic = "No Wi-Fi networks found.";
            }
        }
    } else if (command.status == CommandStatus::timed_out) {
        result.diagnostic = "Wi-Fi scan timed out.";
    } else if (command.status == CommandStatus::output_limit_exceeded) {
        result.diagnostic = "Wi-Fi scan returned too much output.";
    } else {
        result.diagnostic = command.output.empty() ? "Wi-Fi scan failed." : command.output;
    }
    event_queue_.push_latest({next_sequence_++, std::move(result)});
    running_.store(false);
}

}  // namespace micropanel_touch::platform
