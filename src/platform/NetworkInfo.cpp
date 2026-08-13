#include "platform/NetworkInfo.h"

#include "platform/CommandRunner.h"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <map>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

std::string read_first_line(const fs::path& path) {
    std::ifstream stream(path);
    std::string value;
    std::getline(stream, value);
    return value;
}

std::map<std::string, std::vector<std::string>> collect_ipv4_addresses() {
    std::map<std::string, std::vector<std::string>> addresses;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return addresses;
    }
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        char buffer[INET_ADDRSTRLEN]{};
        const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        if (inet_ntop(AF_INET, &address->sin_addr, buffer, sizeof(buffer)) != nullptr) {
            addresses[entry->ifa_name].push_back(buffer);
        }
    }
    freeifaddrs(list);
    return addresses;
}

constexpr auto kNmcliReadTimeout = std::chrono::seconds(2);
constexpr std::size_t kMaximumNmcliOutputBytes = 4096U;

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

std::vector<std::string> split_lines(const std::string& output) {
    std::vector<std::string> lines;
    std::size_t start = 0U;
    while (start < output.size()) {
        const std::size_t end = output.find('\n', start);
        const std::size_t length = (end == std::string::npos ? output.size() : end) - start;
        lines.push_back(trim(output.substr(start, length)));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
    return lines;
}

CommandResult run_nmcli(const std::vector<std::string>& arguments,
                        const std::atomic_bool& cancellation_requested) {
    return CommandRunner::run({"/usr/bin/nmcli", arguments, kNmcliReadTimeout,
                               kMaximumNmcliOutputBytes},
                              cancellation_requested);
}

std::optional<core::ManagedIpv4Profile> collect_managed_ipv4_profile(
    const std::string& interface_name, const std::atomic_bool& cancellation_requested) {
    if (interface_name.empty()) {
        return std::nullopt;
    }
    const CommandResult connection = run_nmcli(
        {"--terse", "--get-values", "GENERAL.CONNECTION", "device", "show", interface_name},
        cancellation_requested);
    if (connection.status != CommandStatus::succeeded || cancellation_requested.load()) {
        return std::nullopt;
    }
    const std::string connection_name = trim(connection.output);
    if (connection_name.empty() || connection_name == "--") {
        return std::nullopt;
    }

    const CommandResult settings = run_nmcli(
        {"--terse", "--get-values", "ipv4.method,ipv4.addresses,ipv4.gateway", "connection",
         "show", connection_name},
        cancellation_requested);
    if (settings.status != CommandStatus::succeeded || cancellation_requested.load()) {
        return std::nullopt;
    }
    const std::vector<std::string> values = split_lines(settings.output);
    if (values.size() != 3U) {
        return std::nullopt;
    }
    return core::ManagedIpv4Profile{interface_name, values[0], values[1], values[2]};
}

}  // namespace

NetworkInfoProvider::NetworkInfoProvider(core::UiEventQueue& event_queue,
                                         std::string managed_interface)
    : event_queue_(event_queue), managed_interface_(std::move(managed_interface)) {}

NetworkInfoProvider::~NetworkInfoProvider() {
    stop();
}

void NetworkInfoProvider::start() {
    if (running_.exchange(true)) {
        return;
    }
    cancellation_requested_.store(false);
    worker_ = std::thread(&NetworkInfoProvider::run, this);
}

void NetworkInfoProvider::stop() {
    cancellation_requested_.store(true);
    const bool was_running = running_.exchange(false);
    wake_condition_.notify_one();
    if (!was_running) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void NetworkInfoProvider::request_managed_ipv4_profile() {
    if (!managed_interface_.empty()) {
        profile_refresh_requested_.store(true);
        wake_condition_.notify_one();
    }
}

core::NetworkSnapshot NetworkInfoProvider::collect_snapshot() {
    core::NetworkSnapshot snapshot;
    const auto ipv4_addresses = collect_ipv4_addresses();
    std::error_code ec;
    const fs::path network_root("/sys/class/net");
    if (!fs::is_directory(network_root, ec)) {
        return snapshot;
    }

    for (const auto& entry : fs::directory_iterator(network_root, ec)) {
        if (ec) {
            break;
        }
        core::NetworkInterfaceStatus status;
        status.name = entry.path().filename().string();
        status.mac_address = read_first_line(entry.path() / "address");
        status.link_state = read_first_line(entry.path() / "operstate");
        status.carrier = read_first_line(entry.path() / "carrier") == "1";
        if (const auto found = ipv4_addresses.find(status.name); found != ipv4_addresses.end()) {
            status.ipv4_addresses = found->second;
        }
        snapshot.interfaces.push_back(std::move(status));
    }
    std::sort(snapshot.interfaces.begin(), snapshot.interfaces.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return snapshot;
}

void NetworkInfoProvider::run() {
    while (running_.load()) {
        event_queue_.push_latest({next_sequence_++, collect_snapshot()});
        if (profile_refresh_requested_.exchange(false)) {
            if (auto profile = collect_managed_ipv4_profile(managed_interface_, cancellation_requested_);
                profile.has_value() && running_.load() && !cancellation_requested_.load()) {
                event_queue_.push_latest({next_sequence_++, std::move(*profile)});
            }
        }
        std::unique_lock<std::mutex> lock(wake_mutex_);
        wake_condition_.wait_for(lock, std::chrono::milliseconds(500), [this] {
            return !running_.load() || profile_refresh_requested_.load();
        });
    }
}

}  // namespace micropanel_touch::platform
