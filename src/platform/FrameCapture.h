#pragma once

#include "core/UiControl.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace micropanel_touch::platform {

// Captures a compact, row-contiguous RGB565 little-endian image. The source
// framebuffer's driver stride and virtual offsets are normalized away before
// the control protocol sees these bytes.
using Rgb565Frame = core::UiFrameCapture;

std::optional<Rgb565Frame> capture_framebuffer_rgb565(const std::filesystem::path& framebuffer,
                                                       std::string* diagnostic);

}  // namespace micropanel_touch::platform
