#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace micropanel_touch::platform {

struct AxisRange {
    int minimum{0};
    int maximum{4095};
};

struct TouchPoint {
    int x{0};
    int y{0};
};

// The raw values that should resolve to the first and last pixel on one axis.
// They are deliberately not an AxisRange: a future panel profile can retain a
// reversed axis without changing the kernel's event-device declaration.
struct TouchAxisCalibration {
    int raw_at_zero{0};
    int raw_at_maximum{4095};
};

struct TouchAxisMappings {
    TouchAxisCalibration x_axis;
    TouchAxisCalibration y_axis;
};

struct TouchCalibration {
    int native_width{0};
    int native_height{0};
    TouchAxisCalibration x_axis;
    TouchAxisCalibration y_axis;
};

struct TouchCalibrationSample {
    TouchPoint raw;
    TouchPoint expected;
};

TouchAxisCalibration default_touch_axis_calibration(AxisRange axis);
bool touch_calibration_is_compatible(const TouchCalibration& calibration,
                                     AxisRange raw_x_axis, AxisRange raw_y_axis,
                                     int native_width, int native_height,
                                     std::string* diagnostic = nullptr);

std::optional<TouchCalibration> solve_touch_calibration(
    const std::vector<TouchCalibrationSample>& samples,
    AxisRange raw_x_axis, AxisRange raw_y_axis,
    int native_width, int native_height,
    std::string* diagnostic = nullptr);

std::optional<TouchCalibration> load_touch_calibration(
    const std::filesystem::path& path, std::string* diagnostic = nullptr);
bool save_touch_calibration(const std::filesystem::path& path,
                            const TouchCalibration& calibration,
                            std::string* diagnostic = nullptr);

}  // namespace micropanel_touch::platform
