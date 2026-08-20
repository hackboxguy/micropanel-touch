#include "platform/SystemStats.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream.good()) {
        return {};
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

std::string first_line(const std::string& contents) {
    const std::string::size_type end = contents.find('\n');
    return end == std::string::npos ? contents : contents.substr(0, end);
}

std::string format_one_decimal(double value) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(1) << value;
    return text.str();
}

// Mebibytes, because a 2 GiB panel reporting "1998848 kB" tells the operator
// nothing they can hold in their head.
std::string format_mib(std::uint64_t kilobytes) {
    std::ostringstream text;
    text << (kilobytes + 512U) / 1024U << " MiB";
    return text.str();
}

std::string format_duration(std::uint64_t seconds) {
    const std::uint64_t days = seconds / 86400U;
    const std::uint64_t hours = (seconds % 86400U) / 3600U;
    const std::uint64_t minutes = (seconds % 3600U) / 60U;
    std::ostringstream text;
    if (days > 0U) {
        text << days << "d ";
    }
    if (days > 0U || hours > 0U) {
        text << hours << "h ";
    }
    text << minutes << "m";
    return text.str();
}

}  // namespace

std::optional<CpuTimeSample> parse_proc_stat_cpu(const std::string& contents) {
    std::istringstream stream(first_line(contents));
    std::string label;
    if (!(stream >> label) || label != "cpu") {
        return std::nullopt;
    }
    // Fields are user, nice, system, idle, iowait, irq, softirq, steal, guest,
    // guest_nice - and the list has grown twice in the kernel's history, so
    // read what is there rather than a fixed count. Idle and iowait are the
    // only non-busy ones; everything else counts as work.
    std::vector<std::uint64_t> fields;
    std::uint64_t field = 0U;
    while (stream >> field) {
        fields.push_back(field);
    }
    if (fields.size() < 4U) {
        return std::nullopt;
    }
    CpuTimeSample sample;
    for (std::vector<std::uint64_t>::size_type index = 0U; index < fields.size(); ++index) {
        sample.total += fields[index];
        if (index != 3U && index != 4U) {
            sample.busy += fields[index];
        }
    }
    return sample;
}

std::optional<std::array<double, 3>> parse_proc_loadavg(const std::string& contents) {
    std::istringstream stream(contents);
    std::array<double, 3> averages{};
    for (double& average : averages) {
        if (!(stream >> average) || average < 0.0) {
            return std::nullopt;
        }
    }
    return averages;
}

std::optional<std::uint64_t> parse_proc_uptime_seconds(const std::string& contents) {
    std::istringstream stream(contents);
    double seconds = 0.0;
    if (!(stream >> seconds) || seconds < 0.0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(seconds);
}

std::pair<std::optional<std::uint64_t>, std::optional<std::uint64_t>> parse_proc_meminfo(
    const std::string& contents) {
    std::optional<std::uint64_t> total;
    std::optional<std::uint64_t> available;
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream fields(line);
        std::string key;
        std::uint64_t value = 0U;
        if (!(fields >> key >> value)) {
            continue;
        }
        if (key == "MemTotal:") {
            total = value;
        } else if (key == "MemAvailable:") {
            available = value;
        }
    }
    return {total, available};
}

std::optional<double> parse_thermal_millidegrees(const std::string& contents) {
    const std::string line = first_line(contents);
    if (line.empty()) {
        return std::nullopt;
    }
    try {
        const double millidegrees = std::stod(line);
        // A thermal zone reading below absolute zero or above the melting point
        // of the board is a parse error wearing a number's clothes.
        const double celsius = millidegrees / 1000.0;
        if (celsius < -100.0 || celsius > 250.0) {
            return std::nullopt;
        }
        return celsius;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::pair<std::string, std::string>> system_stats_rows(const SystemStats& stats) {
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("CPU", stats.cpu_busy_percent.has_value()
                                 ? std::to_string(*stats.cpu_busy_percent) + "%"
                                 : "measuring…");
    if (stats.load_average_1m.has_value() && stats.load_average_5m.has_value() &&
        stats.load_average_15m.has_value()) {
        rows.emplace_back("Load", format_one_decimal(*stats.load_average_1m) + "  " +
                                      format_one_decimal(*stats.load_average_5m) + "  " +
                                      format_one_decimal(*stats.load_average_15m));
    } else {
        rows.emplace_back("Load", "unavailable");
    }
    rows.emplace_back("Temp", stats.cpu_temperature_c.has_value()
                                  ? format_one_decimal(*stats.cpu_temperature_c) + " °C"
                                  : "no sensor");
    if (stats.memory_total_kb.has_value() && stats.memory_available_kb.has_value() &&
        *stats.memory_total_kb > 0U) {
        const std::uint64_t used_kb = *stats.memory_total_kb >= *stats.memory_available_kb
                                          ? *stats.memory_total_kb - *stats.memory_available_kb
                                          : 0U;
        const unsigned int percent = static_cast<unsigned int>(
            (used_kb * 100U + *stats.memory_total_kb / 2U) / *stats.memory_total_kb);
        rows.emplace_back("Memory", format_mib(used_kb) + " / " +
                                        format_mib(*stats.memory_total_kb) + "  (" +
                                        std::to_string(percent) + "%)");
    } else {
        rows.emplace_back("Memory", "unavailable");
    }
    rows.emplace_back("Uptime", stats.uptime_seconds.has_value()
                                    ? format_duration(*stats.uptime_seconds)
                                    : "unavailable");
    return rows;
}

SystemStatsReader::SystemStatsReader(fs::path proc_root, fs::path thermal_zone_root)
    : proc_root_(std::move(proc_root)), thermal_zone_root_(std::move(thermal_zone_root)) {}

SystemStats SystemStatsReader::read() {
    SystemStats stats;

    if (const auto sample = parse_proc_stat_cpu(read_file(proc_root_ / "stat"))) {
        if (previous_cpu_sample_.has_value() && sample->total > previous_cpu_sample_->total) {
            const std::uint64_t total_delta = sample->total - previous_cpu_sample_->total;
            const std::uint64_t busy_delta = sample->busy >= previous_cpu_sample_->busy
                                                 ? sample->busy - previous_cpu_sample_->busy
                                                 : 0U;
            stats.cpu_busy_percent = static_cast<unsigned int>(
                std::min<std::uint64_t>(100U, (busy_delta * 100U + total_delta / 2U) / total_delta));
        }
        previous_cpu_sample_ = sample;
    }

    if (const auto averages = parse_proc_loadavg(read_file(proc_root_ / "loadavg"))) {
        stats.load_average_1m = (*averages)[0];
        stats.load_average_5m = (*averages)[1];
        stats.load_average_15m = (*averages)[2];
    }

    const auto memory = parse_proc_meminfo(read_file(proc_root_ / "meminfo"));
    stats.memory_total_kb = memory.first;
    stats.memory_available_kb = memory.second;

    stats.uptime_seconds = parse_proc_uptime_seconds(read_file(proc_root_ / "uptime"));

    // Zone 0 is the SoC on every Pi this image supports, but the directory is
    // scanned rather than assumed so a board that numbers them differently
    // reports a temperature instead of "no sensor".
    std::error_code error;
    std::vector<fs::path> zones;
    for (const auto& entry : fs::directory_iterator(thermal_zone_root_, error)) {
        if (entry.path().filename().string().rfind("thermal_zone", 0U) == 0U) {
            zones.push_back(entry.path());
        }
    }
    std::sort(zones.begin(), zones.end());
    for (const fs::path& zone : zones) {
        if (const auto celsius = parse_thermal_millidegrees(read_file(zone / "temp"))) {
            stats.cpu_temperature_c = celsius;
            break;
        }
    }

    return stats;
}

}  // namespace micropanel_touch::platform
