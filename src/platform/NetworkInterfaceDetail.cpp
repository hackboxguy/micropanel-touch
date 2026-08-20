#include "platform/NetworkInterfaceDetail.h"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <ifaddrs.h>
#include <iomanip>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

std::string read_first_line(const fs::path& path) {
    std::ifstream stream(path);
    std::string value;
    std::getline(stream, value);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
    return value;
}

std::optional<std::uint64_t> read_counter(const fs::path& path) {
    const std::string value = read_first_line(path);
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        return static_cast<std::uint64_t>(std::stoull(value));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<unsigned int> read_unsigned(const fs::path& path) {
    const std::string value = read_first_line(path);
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        const long parsed = std::stol(value);
        // The kernel reports -1 for a link whose speed it does not know, which
        // is every wireless interface and every ethernet link that is down.
        if (parsed < 0) {
            return std::nullopt;
        }
        return static_cast<unsigned int>(parsed);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::int64_t monotonic_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::map<std::string, std::vector<std::string>> collect_ipv4_with_prefix() {
    std::map<std::string, std::vector<std::string>> addresses;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return addresses;
    }
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET ||
            entry->ifa_name == nullptr) {
            continue;
        }
        char text[INET_ADDRSTRLEN]{};
        const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) == nullptr) {
            continue;
        }
        std::string entry_text = text;
        if (entry->ifa_netmask != nullptr) {
            const auto* netmask = reinterpret_cast<const sockaddr_in*>(entry->ifa_netmask);
            std::uint32_t mask = ntohl(netmask->sin_addr.s_addr);
            unsigned int prefix = 0U;
            while (mask & 0x80000000U) {
                ++prefix;
                mask <<= 1U;
            }
            entry_text += "/" + std::to_string(prefix);
        }
        addresses[entry->ifa_name].push_back(std::move(entry_text));
    }
    freeifaddrs(list);
    return addresses;
}

}  // namespace

std::optional<std::string> parse_default_route_gateway(const std::string& proc_net_route,
                                                       const std::string& interface_name) {
    std::istringstream stream(proc_net_route);
    std::string line;
    std::getline(stream, line);   // the header row
    while (std::getline(stream, line)) {
        std::istringstream fields(line);
        std::string name;
        std::string destination;
        std::string gateway;
        if (!(fields >> name >> destination >> gateway)) {
            continue;
        }
        // A default route is the one to 0.0.0.0. The columns are
        // little-endian hex, which is why the gateway is reassembled backwards.
        if (name != interface_name || destination != "00000000") {
            continue;
        }
        std::uint32_t value = 0U;
        try {
            value = static_cast<std::uint32_t>(std::stoul(gateway, nullptr, 16));
        } catch (const std::exception&) {
            return std::nullopt;
        }
        std::ostringstream text;
        text << (value & 0xFFU) << '.' << ((value >> 8U) & 0xFFU) << '.'
             << ((value >> 16U) & 0xFFU) << '.' << ((value >> 24U) & 0xFFU);
        return text.str();
    }
    return std::nullopt;
}

std::string format_byte_count(std::uint64_t bytes) {
    static constexpr const char* kUnits[] = {"B", "kB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0U;
    while (value >= 1000.0 && unit + 1U < sizeof(kUnits) / sizeof(kUnits[0])) {
        value /= 1000.0;
        ++unit;
    }
    std::ostringstream text;
    text << std::fixed << std::setprecision(unit == 0U ? 0 : 1) << value << ' ' << kUnits[unit];
    return text.str();
}

std::string format_byte_rate(double bytes_per_second) {
    if (bytes_per_second < 0.0) {
        bytes_per_second = 0.0;
    }
    return format_byte_count(static_cast<std::uint64_t>(bytes_per_second + 0.5)) + "/s";
}

std::vector<std::pair<std::string, std::string>> interface_detail_rows(
    const NetworkInterfaceDetail& detail) {
    std::vector<std::pair<std::string, std::string>> rows;

    std::string link = detail.operstate.empty() ? "unknown" : detail.operstate;
    if (!detail.carrier && detail.operstate == "up") {
        link += ", no carrier";
    }
    if (detail.speed_mbps.has_value()) {
        link += "  " + std::to_string(*detail.speed_mbps) + " Mb/s";
        if (!detail.duplex.empty() && detail.duplex != "unknown") {
            link += " " + detail.duplex;
        }
    }
    rows.emplace_back("Link", link);

    rows.emplace_back("IPv4", detail.ipv4_addresses.empty()
                                  ? "none"
                                  : detail.ipv4_addresses.front());
    rows.emplace_back("MAC", detail.mac_address.empty() ? "unknown" : detail.mac_address);
    // Named plainly, because "which one is the way out" is the question two
    // addresses on one panel provokes.
    rows.emplace_back("Route", detail.default_route
                                   ? ("default via " + (detail.gateway.empty() ? std::string("?")
                                                                               : detail.gateway))
                                   : "not the default");
    rows.emplace_back("Rate", detail.rx_bytes_per_second.has_value() &&
                                      detail.tx_bytes_per_second.has_value()
                                  ? ("rx " + format_byte_rate(*detail.rx_bytes_per_second) +
                                     "  tx " + format_byte_rate(*detail.tx_bytes_per_second))
                                  : "measuring...");
    rows.emplace_back("Total", detail.rx_bytes.has_value() && detail.tx_bytes.has_value()
                                   ? ("rx " + format_byte_count(*detail.rx_bytes) + "  tx " +
                                      format_byte_count(*detail.tx_bytes))
                                   : "unavailable");
    // Errors and drops together: on a lab tool these are the numbers that say
    // "the cable is bad" or "the link is saturated", and they are useless
    // unless both directions are visible at once.
    const auto counter = [](const std::optional<std::uint64_t>& value) {
        return value.has_value() ? std::to_string(*value) : std::string("?");
    };
    rows.emplace_back("Dropped", "rx " + counter(detail.rx_dropped) + "  tx " +
                                     counter(detail.tx_dropped));
    rows.emplace_back("Errors", "rx " + counter(detail.rx_errors) + "  tx " +
                                    counter(detail.tx_errors));
    return rows;
}

NetworkInterfaceDetailReader::NetworkInterfaceDetailReader(fs::path sys_class_net,
                                                           fs::path proc_net_route)
    : sys_class_net_(std::move(sys_class_net)), proc_net_route_(std::move(proc_net_route)) {}

std::vector<std::string> NetworkInterfaceDetailReader::interface_names() const {
    std::vector<std::string> names;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(sys_class_net_, error)) {
        names.push_back(entry.path().filename().string());
    }
    // Alphabetical, but loopback last: it is always present and never the one
    // an operator opened this screen to look at.
    std::sort(names.begin(), names.end(), [](const std::string& left, const std::string& right) {
        if ((left == "lo") != (right == "lo")) {
            return right == "lo";
        }
        return left < right;
    });
    return names;
}

NetworkInterfaceDetail NetworkInterfaceDetailReader::read(const std::string& interface_name) {
    NetworkInterfaceDetail detail;
    detail.name = interface_name;
    const fs::path root = sys_class_net_ / interface_name;
    detail.mac_address = read_first_line(root / "address");
    detail.operstate = read_first_line(root / "operstate");
    detail.carrier = read_first_line(root / "carrier") == "1";
    detail.mtu = read_unsigned(root / "mtu");
    detail.speed_mbps = read_unsigned(root / "speed");
    detail.duplex = read_first_line(root / "duplex");

    const auto addresses = collect_ipv4_with_prefix();
    if (const auto found = addresses.find(interface_name); found != addresses.end()) {
        detail.ipv4_addresses = found->second;
    }

    std::ifstream route_stream(proc_net_route_);
    std::ostringstream route_text;
    route_text << route_stream.rdbuf();
    if (const auto gateway = parse_default_route_gateway(route_text.str(), interface_name)) {
        detail.default_route = true;
        detail.gateway = *gateway;
    }

    const fs::path statistics = root / "statistics";
    detail.rx_bytes = read_counter(statistics / "rx_bytes");
    detail.tx_bytes = read_counter(statistics / "tx_bytes");
    detail.rx_packets = read_counter(statistics / "rx_packets");
    detail.tx_packets = read_counter(statistics / "tx_packets");
    detail.rx_errors = read_counter(statistics / "rx_errors");
    detail.tx_errors = read_counter(statistics / "tx_errors");
    detail.rx_dropped = read_counter(statistics / "rx_dropped");
    detail.tx_dropped = read_counter(statistics / "tx_dropped");

    const std::int64_t now = monotonic_millis();
    if (detail.rx_bytes.has_value() && detail.tx_bytes.has_value()) {
        const auto previous = previous_counters_.find(interface_name);
        const auto previous_time = previous_read_millis_.find(interface_name);
        if (previous != previous_counters_.end() && previous_time != previous_read_millis_.end()) {
            const std::int64_t elapsed = now - previous_time->second;
            // A counter that went backwards means the interface was recreated;
            // reporting a negative or enormous rate would be worse than
            // reporting none until the next sample.
            if (elapsed > 0 && *detail.rx_bytes >= previous->second.first &&
                *detail.tx_bytes >= previous->second.second) {
                const double seconds = static_cast<double>(elapsed) / 1000.0;
                detail.rx_bytes_per_second =
                    static_cast<double>(*detail.rx_bytes - previous->second.first) / seconds;
                detail.tx_bytes_per_second =
                    static_cast<double>(*detail.tx_bytes - previous->second.second) / seconds;
            }
        }
        previous_counters_[interface_name] = {*detail.rx_bytes, *detail.tx_bytes};
        previous_read_millis_[interface_name] = now;
    }
    return detail;
}

}  // namespace micropanel_touch::platform
