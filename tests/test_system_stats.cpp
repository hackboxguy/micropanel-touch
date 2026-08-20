#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/SystemStats.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using micropanel_touch::platform::parse_proc_loadavg;
using micropanel_touch::platform::parse_proc_meminfo;
using micropanel_touch::platform::parse_proc_stat_cpu;
using micropanel_touch::platform::parse_proc_uptime_seconds;
using micropanel_touch::platform::parse_thermal_millidegrees;
using micropanel_touch::platform::system_stats_rows;
using micropanel_touch::platform::SystemStats;
using micropanel_touch::platform::SystemStatsReader;

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    assert(stream.good());
    stream << contents;
}

std::string row_value(const SystemStats& stats, const std::string& label) {
    for (const auto& row : system_stats_rows(stats)) {
        if (row.first == label) {
            return row.second;
        }
    }
    return "<missing>";
}

void test_cpu_parsing() {
    // The real first line, with the trailing guest fields the kernel added
    // after this format was first documented. Idle (4th) and iowait (5th) are
    // the only ones that are not work.
    const auto sample = parse_proc_stat_cpu(
        "cpu  100 20 30 1000 40 5 5 0 0 0\ncpu0 1 2 3 4 5 6 7 8 9 10\n");
    assert(sample.has_value());
    assert(sample->total == 1200U);
    assert(sample->busy == 160U);

    // Fewer fields than any kernel emits, and a line that is not the aggregate.
    assert(!parse_proc_stat_cpu("cpu  1 2 3\n").has_value());
    assert(!parse_proc_stat_cpu("cpu0 1 2 3 4 5\n").has_value());
    assert(!parse_proc_stat_cpu("").has_value());

    // A kernel that grows the field list must not break the reader: the extra
    // columns count as busy, which is what they are.
    const auto extended = parse_proc_stat_cpu("cpu  10 0 0 90 0 0 0 0 0 0 7\n");
    assert(extended.has_value());
    assert(extended->total == 107U);
    assert(extended->busy == 17U);
}

void test_other_parsers() {
    const auto averages = parse_proc_loadavg("0.52 0.31 0.14 1/234 5678\n");
    assert(averages.has_value());
    assert(std::abs((*averages)[0] - 0.52) < 1e-9);
    assert(std::abs((*averages)[2] - 0.14) < 1e-9);
    assert(!parse_proc_loadavg("not a load average\n").has_value());

    assert(parse_proc_uptime_seconds("12345.67 98765.43\n").value() == 12345U);
    assert(!parse_proc_uptime_seconds("").has_value());

    const auto memory = parse_proc_meminfo(
        "MemTotal:        3885396 kB\nMemFree:          123456 kB\nMemAvailable:    3500000 kB\n");
    assert(memory.first.value() == 3885396U);
    assert(memory.second.value() == 3500000U);
    // MemAvailable is absent on very old kernels; MemFree must not be silently
    // substituted for it, because they mean different things.
    const auto without_available = parse_proc_meminfo("MemTotal: 100 kB\nMemFree: 50 kB\n");
    assert(without_available.first.value() == 100U);
    assert(!without_available.second.has_value());

    assert(std::abs(parse_thermal_millidegrees("48312\n").value() - 48.312) < 1e-9);
    // A sensor reading that cannot be a temperature is a parse failure, not a
    // number to put on the screen.
    assert(!parse_thermal_millidegrees("999999999\n").has_value());
    assert(!parse_thermal_millidegrees("warm\n").has_value());
    assert(!parse_thermal_millidegrees("").has_value());
}

void test_percentage_needs_two_samples(const std::filesystem::path& work) {
    const std::filesystem::path proc = work / "proc";
    const std::filesystem::path thermal = work / "thermal";
    write_file(proc / "stat", "cpu  0 0 0 1000 0 0 0 0 0 0\n");
    write_file(proc / "loadavg", "0.00 0.00 0.00 1/1 1\n");
    write_file(proc / "meminfo", "MemTotal:        3885396 kB\nMemAvailable:    3000000 kB\n");
    write_file(proc / "uptime", "90061.0 0.0\n");
    write_file(thermal / "thermal_zone0" / "temp", "40000\n");

    SystemStatsReader reader(proc, thermal);
    const SystemStats first = reader.read();
    // The since-boot average is not the current load. Saying "measuring..." is
    // the honest answer to a question that needs two samples.
    assert(!first.cpu_busy_percent.has_value());
    // ASCII, deliberately: the pinned Montserrat subset has no ellipsis
    // glyph, and LVGL draws a missing glyph as a filled box.
    assert(row_value(first, "CPU") == "measuring...");
    assert(std::abs(first.cpu_temperature_c.value() - 40.0) < 1e-9);
    assert(row_value(first, "Uptime") == "1d 1h 1m");
    // 885396 kB used of 3885396 kB, rounded to whole MiB and a whole percent.
    assert(row_value(first, "Memory") == "865 MiB / 3794 MiB  (23%)");
}

void test_percentage_between_samples(const std::filesystem::path& work) {
    const std::filesystem::path proc = work / "proc2";
    const std::filesystem::path thermal = work / "thermal2";
    write_file(proc / "stat", "cpu  0 0 0 1000 0 0 0 0 0 0\n");
    write_file(proc / "loadavg", "1.32 0.55 0.08 1/1 1\n");
    write_file(proc / "meminfo", "MemTotal: 3885396 kB\nMemAvailable: 3000000 kB\n");
    write_file(proc / "uptime", "125.0 0.0\n");

    SystemStatsReader reader(proc, thermal);
    (void)reader.read();
    // 300 more busy ticks against 1000 more total: 30%.
    write_file(proc / "stat", "cpu  300 0 0 1700 0 0 0 0 0 0\n");
    const SystemStats second = reader.read();
    assert(second.cpu_busy_percent.value() == 30U);
    assert(row_value(second, "CPU") == "30%");
    assert(row_value(second, "Load") == "1.3  0.6  0.1");
    // No thermal directory at all: a board without a sensor says so instead of
    // reporting a plausible-looking zero.
    assert(!second.cpu_temperature_c.has_value());
    assert(row_value(second, "Temp") == "no sensor");
    assert(row_value(second, "Uptime") == "2m");

    // A counter that goes backwards (it does, across a suspend or a bad read)
    // must not produce a nonsense percentage.
    write_file(proc / "stat", "cpu  10 0 0 20 0 0 0 0 0 0\n");
    const SystemStats third = reader.read();
    assert(!third.cpu_busy_percent.has_value());
}

void test_missing_files_are_reported_as_missing(const std::filesystem::path& work) {
    SystemStatsReader reader(work / "nothing-here", work / "no-thermal-either");
    const SystemStats stats = reader.read();
    assert(!stats.load_average_1m.has_value());
    assert(!stats.memory_total_kb.has_value());
    assert(!stats.uptime_seconds.has_value());
    assert(row_value(stats, "Load") == "unavailable");
    assert(row_value(stats, "Memory") == "unavailable");
    assert(row_value(stats, "Uptime") == "unavailable");
    // Five rows always, so the screen's layout never depends on what a
    // particular board happens to export.
    assert(system_stats_rows(stats).size() == 5U);
}

}  // namespace

int main() {
    const std::filesystem::path work =
        std::filesystem::temp_directory_path() / "micropanel-touch-system-stats-test";
    std::filesystem::remove_all(work);

    test_cpu_parsing();
    test_other_parsers();
    test_percentage_needs_two_samples(work);
    test_percentage_between_samples(work);
    test_missing_files_are_reported_as_missing(work);

    std::filesystem::remove_all(work);
    std::cout << "system-stats: PASS\n";
    return 0;
}
