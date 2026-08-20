#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace micropanel_touch::platform {

// One sample of the numbers a lab tool's operator actually wants: is this
// panel busy, is it hot, is it out of memory, and how long has it been up.
//
// Every field is optional because every source is a kernel file that a given
// board may not export - a thermal zone in particular is not guaranteed. An
// absent value is reported as absent rather than as a zero, because "0 °C" and
// "this board has no thermal zone" are different facts and only one of them is
// alarming.
struct SystemStats {
    std::optional<double> load_average_1m;
    std::optional<double> load_average_5m;
    std::optional<double> load_average_15m;
    // Busy time as a percentage of the interval between the two most recent
    // reads. Absent on the first read: a percentage needs two samples, and
    // reporting the since-boot average as if it were current would be a lie
    // that looks like a measurement.
    std::optional<unsigned int> cpu_busy_percent;
    std::optional<double> cpu_temperature_c;
    std::optional<std::uint64_t> memory_total_kb;
    std::optional<std::uint64_t> memory_available_kb;
    std::optional<std::uint64_t> uptime_seconds;
};

// Parsers, separated from the files so they can be tested against the awkward
// inputs a real kernel produces rather than only against the happy path.
struct CpuTimeSample {
    std::uint64_t busy{0};
    std::uint64_t total{0};
};

std::optional<CpuTimeSample> parse_proc_stat_cpu(const std::string& contents);
std::optional<std::array<double, 3>> parse_proc_loadavg(const std::string& contents);
std::optional<std::uint64_t> parse_proc_uptime_seconds(const std::string& contents);
std::pair<std::optional<std::uint64_t>, std::optional<std::uint64_t>> parse_proc_meminfo(
    const std::string& contents);
std::optional<double> parse_thermal_millidegrees(const std::string& contents);

// Human-readable rows for the panel, in display order. Formatting lives here
// rather than in the UI so the wording is testable without a framebuffer.
std::vector<std::pair<std::string, std::string>> system_stats_rows(const SystemStats& stats);

// Reads the kernel's files. Holds one previous CPU sample, which is the whole
// reason this is an object rather than a free function.
class SystemStatsReader {
public:
    explicit SystemStatsReader(std::filesystem::path proc_root = "/proc",
                               std::filesystem::path thermal_zone_root =
                                   "/sys/class/thermal");
    SystemStats read();

private:
    std::filesystem::path proc_root_;
    std::filesystem::path thermal_zone_root_;
    std::optional<CpuTimeSample> previous_cpu_sample_;
};

}  // namespace micropanel_touch::platform
