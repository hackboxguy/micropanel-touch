#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/NetworkInterfaceDetail.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

using micropanel_touch::platform::format_byte_count;
using micropanel_touch::platform::format_byte_rate;
using micropanel_touch::platform::interface_detail_rows;
using micropanel_touch::platform::NetworkInterfaceDetail;
using micropanel_touch::platform::NetworkInterfaceDetailReader;
using micropanel_touch::platform::parse_default_route_gateway;

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    assert(stream.good());
    stream << contents;
}

std::string row_value(const NetworkInterfaceDetail& detail, const std::string& label) {
    for (const auto& row : interface_detail_rows(detail)) {
        if (row.first == label) {
            return row.second;
        }
    }
    return "<missing>";
}

void test_default_route_parsing() {
    // The real shape of /proc/net/route: tab-separated, little-endian hex.
    // 0101A8C0 is 192.168.1.1 read back to front.
    const std::string route =
        "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\n"
        "eth0\t00000000\t0101A8C0\t0003\t0\t0\t100\t00000000\t0\t0\t0\n"
        "eth0\t0000A8C0\t00000000\t0001\t0\t0\t100\t00FFFFFF\t0\t0\t0\n"
        "wlan0\t0000A8C0\t00000000\t0001\t0\t0\t600\t00FFFFFF\t0\t0\t0\n";
    assert(parse_default_route_gateway(route, "eth0").value() == "192.168.1.1");
    // wlan0 has an on-link route but no default: it holds an address without
    // being the way out, which is the bench's normal two-interface state.
    assert(!parse_default_route_gateway(route, "wlan0").has_value());
    assert(!parse_default_route_gateway(route, "absent0").has_value());
    assert(!parse_default_route_gateway("", "eth0").has_value());
    // A header alone must not be read as a route.
    assert(!parse_default_route_gateway("Iface\tDestination\tGateway\n", "eth0").has_value());
}

void test_formatting() {
    assert(format_byte_count(0U) == "0 B");
    assert(format_byte_count(999U) == "999 B");
    assert(format_byte_count(1500U) == "1.5 kB");
    assert(format_byte_count(2500000U) == "2.5 MB");
    assert(format_byte_rate(0.0) == "0 B/s");
    assert(format_byte_rate(1500.0) == "1.5 kB/s");
    // A rate can never be negative; a counter that went backwards is reported
    // as no rate at all rather than as a negative one.
    assert(format_byte_rate(-5.0) == "0 B/s");
}

void write_interface(const std::filesystem::path& net, const std::string& name,
                     std::uint64_t rx, std::uint64_t tx) {
    write_file(net / name / "address", "d8:3a:dd:11:22:33\n");
    write_file(net / name / "operstate", "up\n");
    write_file(net / name / "carrier", "1\n");
    write_file(net / name / "mtu", "1500\n");
    write_file(net / name / "speed", "1000\n");
    write_file(net / name / "duplex", "full\n");
    write_file(net / name / "statistics" / "rx_bytes", std::to_string(rx) + "\n");
    write_file(net / name / "statistics" / "tx_bytes", std::to_string(tx) + "\n");
    write_file(net / name / "statistics" / "rx_packets", "100\n");
    write_file(net / name / "statistics" / "tx_packets", "90\n");
    write_file(net / name / "statistics" / "rx_errors", "0\n");
    write_file(net / name / "statistics" / "tx_errors", "2\n");
    write_file(net / name / "statistics" / "rx_dropped", "7\n");
    write_file(net / name / "statistics" / "tx_dropped", "0\n");
}

void test_reader(const std::filesystem::path& work) {
    const std::filesystem::path net = work / "net";
    const std::filesystem::path route = work / "route";
    write_interface(net, "eth0", 1000U, 2000U);
    write_interface(net, "wlan0", 10U, 20U);
    write_file(net / "lo" / "operstate", "unknown\n");
    write_file(route,
               "Iface\tDestination\tGateway \tFlags\n"
               "eth0\t00000000\t0101A8C0\t0003\n");

    NetworkInterfaceDetailReader reader(net, route);

    // Loopback is always present and never what the screen was opened for.
    const auto names = reader.interface_names();
    assert(names.size() == 3U);
    assert(names.front() == "eth0");
    assert(names.back() == "lo");

    const NetworkInterfaceDetail first = reader.read("eth0");
    assert(first.mac_address == "d8:3a:dd:11:22:33");
    assert(first.operstate == "up");
    assert(first.carrier);
    assert(first.speed_mbps.value() == 1000U);
    assert(first.default_route);
    assert(first.gateway == "192.168.1.1");
    assert(first.rx_dropped.value() == 7U);
    // A rate needs two samples; the first read says so rather than reporting
    // the since-boot average as if it were current.
    assert(!first.rx_bytes_per_second.has_value());
    assert(row_value(first, "Rate") == "measuring...");
    assert(row_value(first, "Route") == "default via 192.168.1.1");
    assert(row_value(first, "Link") == "up  1000 Mb/s full");
    assert(row_value(first, "Dropped") == "rx 7  tx 0");

    // An interface with an address and no default route: the bench's own
    // two-link state, and the thing an operator is trying to tell apart.
    const NetworkInterfaceDetail wireless = reader.read("wlan0");
    assert(!wireless.default_route);
    assert(row_value(wireless, "Route") == "not the default");

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    write_interface(net, "eth0", 1000U + 4000U, 2000U + 400U);
    const NetworkInterfaceDetail second = reader.read("eth0");
    assert(second.rx_bytes_per_second.has_value());
    assert(*second.rx_bytes_per_second > *second.tx_bytes_per_second);
    assert(row_value(second, "Rate").find("measuring") == std::string::npos);

    // A counter that went backwards - the interface was recreated - must not
    // produce a negative or enormous rate.
    write_interface(net, "eth0", 5U, 5U);
    const NetworkInterfaceDetail restarted = reader.read("eth0");
    assert(!restarted.rx_bytes_per_second.has_value());
}

void test_absent_interface_reports_absence(const std::filesystem::path& work) {
    NetworkInterfaceDetailReader reader(work / "nothing-here", work / "no-route");
    assert(reader.interface_names().empty());
    const NetworkInterfaceDetail detail = reader.read("eth9");
    assert(detail.mac_address.empty());
    assert(!detail.default_route);
    // Every row still renders, so the screen's layout never depends on what a
    // particular board happens to expose.
    assert(interface_detail_rows(detail).size() == 8U);
    assert(row_value(detail, "MAC") == "unknown");
    assert(row_value(detail, "IPv4") == "none");
    assert(row_value(detail, "Total") == "unavailable");
}

}  // namespace

int main() {
    const std::filesystem::path work =
        std::filesystem::temp_directory_path() / "micropanel-touch-interface-detail-test";
    std::filesystem::remove_all(work);

    test_default_route_parsing();
    test_formatting();
    test_reader(work);
    test_absent_interface_reports_absence(work);

    std::filesystem::remove_all(work);
    std::cout << "network-interface-detail: PASS\n";
    return 0;
}
