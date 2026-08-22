#include "platform/HardwareInfo.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string_view>

namespace micropanel_touch::platform {
namespace {

// Device-tree properties are NUL-terminated strings, and reading one with
// getline leaves the terminator in the middle of a std::string. Strip it, and
// the trailing newline every /proc and /sys file ends with.
std::string read_trimmed(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    std::string text = contents.str();
    text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

std::optional<std::uint64_t> parse_unsigned(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1U);
    }
    std::uint64_t value = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint64_t> read_unsigned(const std::filesystem::path& path) {
    return parse_unsigned(read_trimmed(path));
}

// "Memory: 3.7 GiB" reads better than 3886896 kB, and one decimal is the most
// precision anyone acts on.
std::string format_binary_size(std::uint64_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    constexpr double kMiB = 1024.0 * 1024.0;
    std::array<char, 32> buffer{};
    if (bytes >= static_cast<std::uint64_t>(kGiB)) {
        std::snprintf(buffer.data(), buffer.size(), "%.1f GiB", static_cast<double>(bytes) / kGiB);
    } else {
        std::snprintf(buffer.data(), buffer.size(), "%.0f MiB", static_cast<double>(bytes) / kMiB);
    }
    return buffer.data();
}

// Storage is decimal, deliberately: a card sold as 128 GB reports 127.9 GB in
// these units and 119.1 GiB in the other, and only one of those lets a person
// match the panel against the card in their hand.
std::string format_decimal_size(std::uint64_t bytes) {
    constexpr double kGB = 1000.0 * 1000.0 * 1000.0;
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.0f GB", static_cast<double>(bytes) / kGB);
    return buffer.data();
}

std::string format_frequency(std::uint64_t hertz) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.1f GHz",
                  static_cast<double>(hertz) / 1000000000.0);
    return buffer.data();
}

// One pass over /proc/cpuinfo: how many cores it lists, and what part they are.
void read_cpuinfo(const std::filesystem::path& path, HardwareInfo* info) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }
    std::string line;
    std::string part;
    while (std::getline(file, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0U, colon);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
            key.pop_back();
        }
        const std::string value = line.substr(colon + 1U);
        if (key == "processor") {
            ++info->cpu_cores;
        } else if (key == "CPU part" && part.empty()) {
            part = value;
            while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) {
                part.erase(part.begin());
            }
        }
    }
    if (!part.empty()) {
        info->cpu_name = cpu_part_name(part);
    }
}

}  // namespace

std::string cpu_part_name(const std::string& part_hex) {
    // The parts this product has actually run on, plus the neighbours it is
    // most likely to meet. Anything else keeps its number: a wrong name is
    // worse than a number a person can look up, and this table cannot be kept
    // exhaustive.
    struct Part {
        const char* id;
        const char* name;
    };
    static constexpr std::array<Part, 6> kParts{{
        {"0xd03", "Cortex-A53"},   // Pi 3
        {"0xd07", "Cortex-A57"},
        {"0xd08", "Cortex-A72"},   // Pi 4
        {"0xd0b", "Cortex-A76"},   // Pi 5
        {"0xc07", "Cortex-A7"},    // Pi 2
        {"0xb76", "ARM1176"},      // Pi 1
    }};
    for (const Part& part : kParts) {
        if (part_hex == part.id) {
            return part.name;
        }
    }
    return "part " + part_hex;
}

HardwareInfo read_hardware_info(const HardwarePaths& paths) {
    HardwareInfo info;
    info.board = read_trimmed(paths.board_model);
    info.serial = read_trimmed(paths.board_serial);
    info.kernel = read_trimmed(paths.kernel_release);
    read_cpuinfo(paths.cpuinfo, &info);

    if (const std::optional<std::uint64_t> kilohertz = read_unsigned(paths.cpu_max_frequency);
        kilohertz.has_value()) {
        info.cpu_max_hz = *kilohertz * 1000U;
    }

    // MemTotal is what the kernel has, which is less than what is soldered on:
    // the firmware keeps some for itself. Reporting the usable figure is the
    // one that can be measured rather than inferred from a revision table.
    std::ifstream meminfo(paths.meminfo);
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemTotal:", 0U) != 0U) {
            continue;
        }
        if (const std::optional<std::uint64_t> kilobytes =
                parse_unsigned(std::string_view(line).substr(std::string_view("MemTotal:").size()));
            kilobytes.has_value()) {
            info.memory_bytes = *kilobytes * 1024U;
        }
        break;
    }

    // /sys/block/<device>/size is in 512-byte sectors regardless of the
    // device's own block size - a kernel ABI, not a property of the card.
    if (const std::optional<std::uint64_t> sectors = read_unsigned(paths.storage_size);
        sectors.has_value()) {
        info.storage_bytes = *sectors * 512U;
    }
    info.storage_name = read_trimmed(paths.storage_name);
    return info;
}

std::vector<std::pair<std::string, std::string>> hardware_rows(const HardwareInfo& info) {
    std::vector<std::pair<std::string, std::string>> rows;
    if (!info.board.empty()) {
        rows.emplace_back("Board", info.board);
    }
    if (info.cpu_cores > 0U || !info.cpu_name.empty()) {
        std::string cpu;
        if (info.cpu_cores > 0U) {
            // "x" and not the multiplication sign: the pinned font is barely
            // more than ASCII, and a glyph it lacks draws as a filled box.
            cpu = std::to_string(info.cpu_cores) + "x ";
        }
        cpu += info.cpu_name.empty() ? std::string("CPU") : info.cpu_name;
        if (info.cpu_max_hz.has_value()) {
            cpu += " @ " + format_frequency(*info.cpu_max_hz);
        }
        rows.emplace_back("CPU", cpu);
    }
    if (info.memory_bytes.has_value()) {
        rows.emplace_back("Memory", format_binary_size(*info.memory_bytes));
    }
    if (info.storage_bytes.has_value()) {
        std::string storage = format_decimal_size(*info.storage_bytes);
        if (!info.storage_name.empty()) {
            storage += " " + info.storage_name;
        }
        rows.emplace_back("Storage", storage);
    }
    if (!info.kernel.empty()) {
        rows.emplace_back("Kernel", info.kernel);
    }
    if (!info.serial.empty()) {
        rows.emplace_back("Serial", info.serial);
    }
    return rows;
}

}  // namespace micropanel_touch::platform
