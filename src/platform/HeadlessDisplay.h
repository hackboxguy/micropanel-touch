#pragma once

#include "core/UiControl.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <lvgl.h>

namespace micropanel_touch::platform {

// An LVGL display target backed by process memory. It exercises the same
// renderer and render-settle path as the panel backend without opening a DRM
// or framebuffer device, so UI control/capture tests can run in host CI.
// Call lv_init() before constructing it and destroy it before lv_deinit().
class HeadlessDisplay {
public:
    HeadlessDisplay(std::uint32_t width, std::uint32_t height);
    ~HeadlessDisplay();
    HeadlessDisplay(const HeadlessDisplay&) = delete;
    HeadlessDisplay& operator=(const HeadlessDisplay&) = delete;

    lv_display_t* display() const;
    std::optional<core::UiFrameCapture> capture(std::string* diagnostic) const;

private:
    static void flush_callback(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixels);

    lv_display_t* display_{nullptr};
    std::uint32_t width_{0};
    std::uint32_t height_{0};
    std::uint32_t draw_stride_bytes_{0};
    std::vector<std::uint8_t> backing_pixels_;
};

}  // namespace micropanel_touch::platform
