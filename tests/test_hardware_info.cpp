// The hardware summary, against the files a Pi actually publishes.
//
// Every fixture below is a copy of what the bench panel reports, including the
// awkward parts: the device tree's NUL terminator, /sys/block's 512-byte
// sectors regardless of the card's real block size, and MemTotal being smaller
// than the memory soldered on.
#include "platform/HardwareInfo.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

using micropanel_touch::platform::HardwareInfo;
using micropanel_touch::platform::HardwarePaths;

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

std::string value_of(const std::vector<std::pair<std::string, std::string>>& rows,
                     const std::string& name) {
    for (const auto& row : rows) {
        if (row.first == name) {
            return row.second;
        }
    }
    return {};
}

bool has_row(const std::vector<std::pair<std::string, std::string>>& rows,
             const std::string& name) {
    for (const auto& row : rows) {
        if (row.first == name) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("hardware-info-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);

    HardwarePaths paths;
    paths.board_model = directory / "model";
    paths.board_serial = directory / "serial-number";
    paths.cpuinfo = directory / "cpuinfo";
    paths.cpu_max_frequency = directory / "cpuinfo_max_freq";
    paths.meminfo = directory / "meminfo";
    paths.kernel_release = directory / "osrelease";
    paths.storage_size = directory / "size";
    paths.storage_name = directory / "name";

    // Exactly what the bench panel reports, NUL terminators included.
    write_file(paths.board_model, std::string("Raspberry Pi 4 Model B Rev 1.5\0", 31U));
    write_file(paths.board_serial, std::string("100000003d1dc9e0\0", 17U));
    write_file(paths.cpuinfo,
               "processor\t: 0\n"
               "BogoMIPS\t: 108.00\n"
               "CPU implementer\t: 0x41\n"
               "CPU part\t: 0xd08\n"
               "\n"
               "processor\t: 1\n"
               "CPU part\t: 0xd08\n"
               "\n"
               "processor\t: 2\n"
               "CPU part\t: 0xd08\n"
               "\n"
               "processor\t: 3\n"
               "CPU part\t: 0xd08\n"
               "\n"
               "Revision\t: c03115\n"
               "Model\t\t: Raspberry Pi 4 Model B Rev 1.5\n");
    write_file(paths.cpu_max_frequency, "1800000\n");
    write_file(paths.meminfo,
               "MemTotal:        3886896 kB\n"
               "MemFree:          123456 kB\n");
    write_file(paths.kernel_release, "6.18.39+rpt-rpi-v8\n");
    write_file(paths.storage_size, "249737216\n");
    write_file(paths.storage_name, "SN128\n");

    {
        const HardwareInfo info = micropanel_touch::platform::read_hardware_info(paths);
        // The NUL must not survive into the string: it terminates the value in
        // every C API downstream, and LVGL would draw the text up to it and
        // stop - which looks like a truncation bug in the screen.
        assert(info.board == "Raspberry Pi 4 Model B Rev 1.5");
        assert(info.board.find('\0') == std::string::npos);
        assert(info.serial == "100000003d1dc9e0");
        assert(info.kernel == "6.18.39+rpt-rpi-v8");
        assert(info.cpu_cores == 4U);
        assert(info.cpu_name == "Cortex-A72");
        assert(info.cpu_max_hz.has_value() && *info.cpu_max_hz == 1800000000ULL);
        assert(info.memory_bytes.has_value() && *info.memory_bytes == 3886896ULL * 1024ULL);
        assert(info.storage_bytes.has_value() && *info.storage_bytes == 249737216ULL * 512ULL);
        assert(info.storage_name == "SN128");

        const auto rows = micropanel_touch::platform::hardware_rows(info);
        assert(value_of(rows, "Board") == "Raspberry Pi 4 Model B Rev 1.5");
        assert(value_of(rows, "CPU") == "4x Cortex-A72 @ 1.8 GHz");
        assert(value_of(rows, "Memory") == "3.7 GiB");
        // A card sold as 128 GB: decimal, so the panel and the label on the
        // card agree. In binary units the same card reads 119.1 GiB, which is
        // correct and matches nothing a person can hold.
        assert(value_of(rows, "Storage") == "128 GB SN128");
        assert(value_of(rows, "Kernel") == "6.18.39+rpt-rpi-v8");
        assert(value_of(rows, "Serial") == "100000003d1dc9e0");

        // Every value has to be renderable by a font that is barely more than
        // ASCII, so the multiplication sign is spelled "x".
        for (const auto& row : rows) {
            for (const char character : row.second) {
                assert(static_cast<unsigned char>(character) < 0x80U &&
                       "a hardware row carries a character the pinned font may not have");
            }
        }
    }

    {
        // A board that publishes none of this - which is what every path looks
        // like on a build host - must produce no rows rather than rows full of
        // "unknown". A screen with nothing to say says nothing.
        HardwarePaths missing;
        missing.board_model = directory / "absent";
        missing.board_serial = directory / "absent";
        missing.cpuinfo = directory / "absent";
        missing.cpu_max_frequency = directory / "absent";
        missing.meminfo = directory / "absent";
        missing.kernel_release = directory / "absent";
        missing.storage_size = directory / "absent";
        missing.storage_name = directory / "absent";
        const HardwareInfo info = micropanel_touch::platform::read_hardware_info(missing);
        const auto rows = micropanel_touch::platform::hardware_rows(info);
        assert(rows.empty());
    }

    {
        // An unknown part keeps its number. A table of these cannot be kept
        // exhaustive, and a wrong name is worse than a number to look up.
        assert(micropanel_touch::platform::cpu_part_name("0xd08") == "Cortex-A72");
        assert(micropanel_touch::platform::cpu_part_name("0xd0b") == "Cortex-A76");
        assert(micropanel_touch::platform::cpu_part_name("0xfff") == "part 0xfff");

        write_file(paths.cpuinfo, "processor\t: 0\nCPU part\t: 0xfff\n");
        const HardwareInfo info = micropanel_touch::platform::read_hardware_info(paths);
        assert(info.cpu_cores == 1U);
        const auto rows = micropanel_touch::platform::hardware_rows(info);
        assert(value_of(rows, "CPU") == "1x part 0xfff @ 1.8 GHz");
    }

    {
        // Half a set of readings is normal on a board that is not a Pi: report
        // what was read and leave out what was not.
        HardwarePaths partial = paths;
        partial.storage_size = directory / "absent";
        partial.board_serial = directory / "absent";
        const HardwareInfo info = micropanel_touch::platform::read_hardware_info(partial);
        const auto rows = micropanel_touch::platform::hardware_rows(info);
        assert(!has_row(rows, "Storage"));
        assert(!has_row(rows, "Serial"));
        assert(has_row(rows, "Memory"));
    }

    std::filesystem::remove_all(directory);
    std::cout << "hardware info: board, cpu, memory, storage and kernel read from real shapes\n";
    return 0;
}
