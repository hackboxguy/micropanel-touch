#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace micropanel_touch::platform {

// What this box *is*, as opposed to what it is doing (System Stats) or what it
// is running (the rest of About).
//
// Every field is read from a file the kernel or the firmware publishes, so a
// panel can answer "which board, how much memory, how big is the card" without
// anyone opening the case or logging in. That is the question an operator asks
// when a device is one of several on a bench, and the answer is otherwise on a
// sticker.
struct HardwareInfo {
    std::string board;             // "Raspberry Pi 4 Model B Rev 1.5"
    std::string serial;            // the firmware's own serial number
    std::string kernel;            // "6.18.39+rpt-rpi-v8"
    std::string cpu_name;          // "Cortex-A72", or "part 0xd08" when unknown
    unsigned int cpu_cores{0U};
    std::optional<std::uint64_t> cpu_max_hz;
    std::optional<std::uint64_t> memory_bytes;
    std::optional<std::uint64_t> storage_bytes;
    std::string storage_name;      // the card's own product name, e.g. "SN128"
};

struct HardwarePaths {
    std::filesystem::path board_model{"/proc/device-tree/model"};
    std::filesystem::path board_serial{"/proc/device-tree/serial-number"};
    std::filesystem::path cpuinfo{"/proc/cpuinfo"};
    std::filesystem::path cpu_max_frequency{
        "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"};
    std::filesystem::path meminfo{"/proc/meminfo"};
    std::filesystem::path kernel_release{"/proc/sys/kernel/osrelease"};
    // The card, not the partition: this is the whole device's size, which is
    // what someone comparing it against the box it came in wants.
    std::filesystem::path storage_size{"/sys/block/mmcblk0/size"};
    std::filesystem::path storage_name{"/sys/block/mmcblk0/device/name"};
};

// The ARM part number an implementer publishes, as the name people use for it.
// Unknown parts come back as their own hex, which is more useful than "unknown"
// and cannot go stale.
std::string cpu_part_name(const std::string& part_hex);

HardwareInfo read_hardware_info(const HardwarePaths& paths = {});

// Display rows, in order. Formatting lives here so the wording is testable
// without a framebuffer.
std::vector<std::pair<std::string, std::string>> hardware_rows(const HardwareInfo& info);

}  // namespace micropanel_touch::platform
