#include "platform/NetworkInfo.h"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <map>
#include <netinet/in.h>
#include <string>
#include <thread>

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

}  // namespace

NetworkInfoProvider::NetworkInfoProvider(core::UiEventQueue& event_queue)
    : event_queue_(event_queue) {}

NetworkInfoProvider::~NetworkInfoProvider() {
    stop();
}

void NetworkInfoProvider::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&NetworkInfoProvider::run, this);
}

void NetworkInfoProvider::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
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
        event_queue_.push({next_sequence_++, collect_snapshot()});
        for (int interval = 0; interval < 10 && running_.load(); ++interval) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

}  // namespace micropanel_touch::platform
