#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace micropanel_touch::platform {

// Captures a compact, row-contiguous RGB565 little-endian image. The source
// framebuffer's driver stride and virtual offsets are normalized away before
// the control protocol sees these bytes.
struct Rgb565Frame {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t stride_bytes{0};
    std::vector<std::uint8_t> pixels;
};

std::optional<Rgb565Frame> capture_framebuffer_rgb565(const std::filesystem::path& framebuffer,
                                                       std::string* diagnostic);

}  // namespace micropanel_touch::platform
