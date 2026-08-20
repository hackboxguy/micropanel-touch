#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace micropanel_touch::platform {

// Everything a lab tool's operator wants to know about one interface, read
// from the kernel's own files. No commands, no privilege: /sys/class/net and
// /proc/net/route are world-readable, so this is the same shape as SystemStats
// and can be read straight from the UI thread.
struct NetworkInterfaceDetail {
    std::string name;
    std::string mac_address;
    std::string operstate;
    bool carrier{false};
    std::optional<unsigned int> mtu;
    // Ethernet link speed. Absent when the link is down or the driver does not
    // report one, which is normal for wireless - not an error to display.
    std::optional<unsigned int> speed_mbps;
    std::string duplex;
    std::vector<std::string> ipv4_addresses;
    // Which interface actually carries traffic off the panel. With two links
    // on one subnet - the bench's normal state - this is the difference
    // between "has an address" and "is the way out", and it is the question
    // two addresses on one device provokes.
    bool default_route{false};
    std::string gateway;

    std::optional<std::uint64_t> rx_bytes;
    std::optional<std::uint64_t> tx_bytes;
    std::optional<std::uint64_t> rx_packets;
    std::optional<std::uint64_t> tx_packets;
    std::optional<std::uint64_t> rx_errors;
    std::optional<std::uint64_t> tx_errors;
    std::optional<std::uint64_t> rx_dropped;
    std::optional<std::uint64_t> tx_dropped;
    // Bytes per second across the interval between the two most recent reads.
    // Absent on the first read: a rate needs two samples, and the since-boot
    // average is not the current rate.
    std::optional<double> rx_bytes_per_second;
    std::optional<double> tx_bytes_per_second;
};

// Parsers, separated from the files so the awkward cases can be tested.
std::optional<std::string> parse_default_route_gateway(const std::string& proc_net_route,
                                                       const std::string& interface_name);
std::string format_byte_rate(double bytes_per_second);
std::string format_byte_count(std::uint64_t bytes);

// Display rows for the detail screen, in order. Formatting lives here so the
// wording is testable without a framebuffer.
std::vector<std::pair<std::string, std::string>> interface_detail_rows(
    const NetworkInterfaceDetail& detail);

class NetworkInterfaceDetailReader {
public:
    explicit NetworkInterfaceDetailReader(std::filesystem::path sys_class_net = "/sys/class/net",
                                          std::filesystem::path proc_net_route = "/proc/net/route");
    // Names of every interface the kernel currently exposes, loopback last.
    std::vector<std::string> interface_names() const;
    NetworkInterfaceDetail read(const std::string& interface_name);

private:
    std::filesystem::path sys_class_net_;
    std::filesystem::path proc_net_route_;
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> previous_counters_;
    std::map<std::string, std::int64_t> previous_read_millis_;
};

}  // namespace micropanel_touch::platform
