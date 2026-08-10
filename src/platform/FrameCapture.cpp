#include "platform/FrameCapture.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace micropanel_touch::platform {
namespace {

constexpr std::size_t kMaximumFrameBytes = 4U * 1024U * 1024U;

bool fail(std::string* diagnostic, const std::string& message) {
    if (diagnostic != nullptr) {
        *diagnostic = message;
    }
    return false;
}

bool checked_multiply(std::size_t left, std::size_t right, std::size_t* result) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    *result = left * right;
    return true;
}

bool read_exact_at(int fd, std::uint8_t* destination, std::size_t bytes, off_t offset) {
    std::size_t copied = 0U;
    while (copied < bytes) {
        const ssize_t received = pread(fd, destination + copied, bytes - copied,
                                       offset + static_cast<off_t>(copied));
        if (received <= 0) {
            return false;
        }
        copied += static_cast<std::size_t>(received);
    }
    return true;
}

}  // namespace

std::optional<Rgb565Frame> capture_framebuffer_rgb565(const std::filesystem::path& framebuffer,
                                                       std::string* diagnostic) {
    const int fd = open(framebuffer.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fail(diagnostic, "unable to open framebuffer for capture: " + std::string(std::strerror(errno)));
        return std::nullopt;
    }

    fb_fix_screeninfo fixed{};
    fb_var_screeninfo variable{};
    const bool information_available =
        ioctl(fd, FBIOGET_FSCREENINFO, &fixed) == 0 && ioctl(fd, FBIOGET_VSCREENINFO, &variable) == 0;
    if (!information_available) {
        close(fd);
        fail(diagnostic, "unable to inspect framebuffer capture format");
        return std::nullopt;
    }

    const bool rgb565 = variable.bits_per_pixel == 16U && variable.red.offset == 11U &&
                        variable.red.length == 5U && variable.green.offset == 5U &&
                        variable.green.length == 6U && variable.blue.offset == 0U &&
                        variable.blue.length == 5U && variable.transp.length == 0U;
    if (!rgb565) {
        close(fd);
        fail(diagnostic, "framebuffer format is not RGB565 little-endian");
        return std::nullopt;
    }
    if (variable.xres == 0U || variable.yres == 0U || variable.xoffset > variable.xres_virtual ||
        variable.yoffset > variable.yres_virtual || variable.xres > variable.xres_virtual - variable.xoffset ||
        variable.yres > variable.yres_virtual - variable.yoffset) {
        close(fd);
        fail(diagnostic, "framebuffer geometry is invalid for capture");
        return std::nullopt;
    }

    std::size_t row_bytes = 0U;
    std::size_t frame_bytes = 0U;
    if (!checked_multiply(variable.xres, 2U, &row_bytes) ||
        !checked_multiply(row_bytes, variable.yres, &frame_bytes) ||
        row_bytes > fixed.line_length || frame_bytes > kMaximumFrameBytes) {
        close(fd);
        fail(diagnostic, "framebuffer capture exceeds supported bounds");
        return std::nullopt;
    }

    std::size_t first_row = 0U;
    std::size_t first_pixel = 0U;
    std::size_t first_offset = 0U;
    if (!checked_multiply(variable.yoffset, fixed.line_length, &first_row) ||
        !checked_multiply(variable.xoffset, 2U, &first_pixel) ||
        first_row > std::numeric_limits<std::size_t>::max() - first_pixel) {
        close(fd);
        fail(diagnostic, "framebuffer capture offset overflows");
        return std::nullopt;
    }
    first_offset = first_row + first_pixel;
    std::size_t final_row = 0U;
    if (!checked_multiply(variable.yres - 1U, fixed.line_length, &final_row) ||
        first_offset > std::numeric_limits<std::size_t>::max() - final_row ||
        first_offset + final_row > std::numeric_limits<std::size_t>::max() - row_bytes ||
        first_offset + final_row + row_bytes > fixed.smem_len) {
        close(fd);
        fail(diagnostic, "framebuffer capture lies outside video memory");
        return std::nullopt;
    }

    Rgb565Frame frame;
    frame.width = variable.xres;
    frame.height = variable.yres;
    frame.stride_bytes = static_cast<std::uint32_t>(row_bytes);
    frame.pixels.resize(frame_bytes);
    for (std::uint32_t row = 0U; row < variable.yres; ++row) {
        const std::size_t source_offset = first_offset + static_cast<std::size_t>(row) * fixed.line_length;
        if (!read_exact_at(fd, frame.pixels.data() + static_cast<std::size_t>(row) * row_bytes,
                           row_bytes, static_cast<off_t>(source_offset))) {
            close(fd);
            fail(diagnostic, "unable to read framebuffer capture data");
            return std::nullopt;
        }
    }
    close(fd);
    return frame;
}

}  // namespace micropanel_touch::platform
