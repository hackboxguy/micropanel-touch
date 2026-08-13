#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/TouchCalibration.h"
#include "platform/TouchInput.h"

#include <cassert>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

using micropanel_touch::platform::AxisRange;
using micropanel_touch::platform::TouchCalibration;
using micropanel_touch::platform::TouchCalibrationSample;
using micropanel_touch::platform::TouchMapper;
using micropanel_touch::platform::TouchPoint;

namespace {

TouchPoint raw_for_expected(TouchPoint expected) {
    // A deliberately offset panel whose effective active area does not span
    // the driver's advertised 0..4095 range.
    return {280 + expected.x * 11, 180 + expected.y * 7};
}

}  // namespace

int main() {
    constexpr int kWidth = 320;
    constexpr int kHeight = 480;
    const std::vector<TouchPoint> targets{
        {36, 110}, {283, 110}, {283, 372}, {36, 372}, {160, 240},
    };
    std::vector<TouchCalibrationSample> samples;
    samples.reserve(targets.size());
    for (const TouchPoint target : targets) {
        samples.push_back({raw_for_expected(target), target});
    }
    std::string diagnostic;
    const auto calibration = micropanel_touch::platform::solve_touch_calibration(
        samples, {0, 4095}, {0, 4095}, kWidth, kHeight, &diagnostic);
    assert(calibration.has_value());
    assert(micropanel_touch::platform::touch_calibration_is_compatible(
        *calibration, {0, 4095}, {0, 4095}, kWidth, kHeight, &diagnostic));
    const TouchMapper mapper({calibration->x_axis, calibration->y_axis}, kWidth, kHeight);
    assert(mapper.map(280, 180).x == 0);
    assert(mapper.map(280, 180).y == 0);
    assert(mapper.map(280 + (kWidth - 1) * 11, 180 + (kHeight - 1) * 7).x == kWidth - 1);
    assert(mapper.map(280 + (kWidth - 1) * 11, 180 + (kHeight - 1) * 7).y == kHeight - 1);

    const fs::path directory = fs::temp_directory_path() /
                               ("micropanel-touch-calibration-" + std::to_string(getpid()));
    const fs::path path = directory / "touch-calibration.conf";
    std::error_code error;
    fs::create_directories(directory, error);
    assert(!error);
    assert(micropanel_touch::platform::save_touch_calibration(path, *calibration, &diagnostic));
    const auto loaded = micropanel_touch::platform::load_touch_calibration(path, &diagnostic);
    assert(loaded.has_value());
    assert(loaded->native_width == calibration->native_width);
    assert(loaded->native_height == calibration->native_height);
    assert(loaded->x_axis.raw_at_zero == calibration->x_axis.raw_at_zero);
    assert(loaded->y_axis.raw_at_maximum == calibration->y_axis.raw_at_maximum);

    const TouchCalibration wrong_geometry{480, 320, calibration->x_axis, calibration->y_axis};
    assert(!micropanel_touch::platform::touch_calibration_is_compatible(
        wrong_geometry, {0, 4095}, {0, 4095}, kWidth, kHeight, &diagnostic));
    const TouchCalibrationSample flat_sample{{1000, 1000}, {100, 100}};
    std::vector<TouchCalibrationSample> flat_samples(5U, flat_sample);
    assert(!micropanel_touch::platform::solve_touch_calibration(
        flat_samples, {0, 4095}, {0, 4095}, kWidth, kHeight, &diagnostic).has_value());

    fs::remove_all(directory, error);
    assert(!error);
    return 0;
}
