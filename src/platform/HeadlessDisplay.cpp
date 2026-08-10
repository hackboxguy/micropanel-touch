#include "platform/HeadlessDisplay.h"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace micropanel_touch::platform {

HeadlessDisplay::HeadlessDisplay(std::uint32_t width, std::uint32_t height)
    : width_(width), height_(height) {
    if (width == 0U || height == 0U ||
        width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        width > std::numeric_limits<std::uint32_t>::max() / 2U) {
        throw std::invalid_argument("headless display dimensions are invalid");
    }
    draw_stride_bytes_ = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    if (draw_stride_bytes_ == 0U ||
        static_cast<std::size_t>(draw_stride_bytes_) >
            std::numeric_limits<std::size_t>::max() / height) {
        throw std::invalid_argument("headless display dimensions exceed supported buffer bounds");
    }
    const std::size_t byte_count = static_cast<std::size_t>(draw_stride_bytes_) * height;
    if (byte_count == 0U ||
        byte_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("headless display dimensions exceed supported buffer bounds");
    }
    backing_pixels_.resize(byte_count);
    display_ = lv_display_create(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
    if (display_ == nullptr) {
        throw std::runtime_error("unable to create headless LVGL display");
    }
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display_, backing_pixels_.data(), nullptr,
                           static_cast<std::uint32_t>(backing_pixels_.size()),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display_, flush_callback);
}

HeadlessDisplay::~HeadlessDisplay() {
    if (display_ != nullptr) {
        lv_display_delete(display_);
    }
}

lv_display_t* HeadlessDisplay::display() const {
    return display_;
}

std::optional<core::UiFrameCapture> HeadlessDisplay::capture(std::string* diagnostic) const {
    const std::uint32_t capture_stride_bytes = width_ * 2U;
    if (draw_stride_bytes_ < capture_stride_bytes ||
        static_cast<std::size_t>(draw_stride_bytes_) * height_ != backing_pixels_.size()) {
        if (diagnostic != nullptr) {
            *diagnostic = "headless RGB565 buffer layout is invalid";
        }
        return std::nullopt;
    }

    core::UiFrameCapture frame;
    frame.width = width_;
    frame.height = height_;
    frame.stride_bytes = capture_stride_bytes;
    frame.pixels.resize(static_cast<std::size_t>(capture_stride_bytes) * height_);
    for (std::uint32_t row = 0U; row < height_; ++row) {
        std::memcpy(frame.pixels.data() + static_cast<std::size_t>(row) * capture_stride_bytes,
                    backing_pixels_.data() + static_cast<std::size_t>(row) * draw_stride_bytes_,
                    capture_stride_bytes);
    }
    return frame;
}

void HeadlessDisplay::flush_callback(lv_display_t* display, const lv_area_t*, std::uint8_t*) {
    // DIRECT mode already rendered into backing_pixels_. Completing the flush
    // is enough for lv_refr_now() to be the same settle barrier used on Pi.
    lv_display_flush_ready(display);
}

}  // namespace micropanel_touch::platform
