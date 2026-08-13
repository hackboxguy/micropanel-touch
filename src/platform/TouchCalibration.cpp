#include "platform/TouchCalibration.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

constexpr std::size_t kRequiredSampleCount = 5U;
constexpr int kCalibrationVersion = 1;
constexpr int kMaximumFitErrorPixels = 18;
constexpr std::size_t kMaximumCalibrationFileBytes = 512U;
constexpr std::array<std::string_view, 7> kKeys{
    "version", "native_width", "native_height", "x_raw_at_zero",
    "x_raw_at_maximum", "y_raw_at_zero", "y_raw_at_maximum",
};

void set_diagnostic(std::string* diagnostic, std::string message) {
    if (diagnostic != nullptr) {
        *diagnostic = std::move(message);
    }
}

bool parse_integer(std::string_view text, int* value) {
    if (value == nullptr || text.empty()) {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), *value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool write_all(int fd, std::string_view content) {
    std::size_t offset = 0U;
    while (offset < content.size()) {
        const ssize_t written = ::write(fd, content.data() + offset, content.size() - offset);
        if (written <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool validate_axis(TouchAxisCalibration calibration, AxisRange raw_axis,
                   std::string_view axis_name, std::string* diagnostic) {
    if (raw_axis.maximum <= raw_axis.minimum) {
        set_diagnostic(diagnostic, std::string(axis_name) + " driver range is invalid");
        return false;
    }
    if (calibration.raw_at_maximum <= calibration.raw_at_zero) {
        set_diagnostic(diagnostic, std::string(axis_name) + " calibration must increase");
        return false;
    }
    const long long driver_span = static_cast<long long>(raw_axis.maximum) - raw_axis.minimum;
    const long long calibration_span =
        static_cast<long long>(calibration.raw_at_maximum) - calibration.raw_at_zero;
    if (calibration_span < driver_span / 3LL || calibration_span > driver_span * 3LL) {
        set_diagnostic(diagnostic, std::string(axis_name) + " calibration span is implausible");
        return false;
    }
    const long long allowance = std::max(1LL, driver_span / 8LL);
    if (static_cast<long long>(calibration.raw_at_zero) < raw_axis.minimum - allowance ||
        static_cast<long long>(calibration.raw_at_maximum) > raw_axis.maximum + allowance) {
        set_diagnostic(diagnostic, std::string(axis_name) + " calibration lies outside the driver range");
        return false;
    }
    return true;
}

std::optional<TouchAxisCalibration> fit_axis(const std::vector<TouchCalibrationSample>& samples,
                                              bool x_axis, int extent,
                                              std::string_view axis_name,
                                              std::string* diagnostic) {
    long double mean_raw = 0.0L;
    long double mean_expected = 0.0L;
    for (const auto& sample : samples) {
        mean_raw += x_axis ? sample.raw.x : sample.raw.y;
        mean_expected += x_axis ? sample.expected.x : sample.expected.y;
    }
    mean_raw /= static_cast<long double>(samples.size());
    mean_expected /= static_cast<long double>(samples.size());

    long double denominator = 0.0L;
    long double numerator = 0.0L;
    for (const auto& sample : samples) {
        const long double raw = x_axis ? sample.raw.x : sample.raw.y;
        const long double expected = x_axis ? sample.expected.x : sample.expected.y;
        const long double raw_delta = raw - mean_raw;
        denominator += raw_delta * raw_delta;
        numerator += raw_delta * (expected - mean_expected);
    }
    if (denominator <= 0.0L) {
        set_diagnostic(diagnostic, std::string(axis_name) + " samples do not span the panel");
        return std::nullopt;
    }
    const long double slope = numerator / denominator;
    if (slope <= 0.0L) {
        set_diagnostic(diagnostic, std::string(axis_name) + " samples have the wrong order");
        return std::nullopt;
    }
    const long double intercept = mean_expected - slope * mean_raw;
    long double squared_error = 0.0L;
    for (const auto& sample : samples) {
        const long double raw = x_axis ? sample.raw.x : sample.raw.y;
        const long double expected = x_axis ? sample.expected.x : sample.expected.y;
        const long double residual = slope * raw + intercept - expected;
        squared_error += residual * residual;
    }
    const long double rms_error = std::sqrt(squared_error / static_cast<long double>(samples.size()));
    if (rms_error > kMaximumFitErrorPixels) {
        set_diagnostic(diagnostic, std::string(axis_name) + " taps were too inconsistent; try again");
        return std::nullopt;
    }

    const long double raw_at_zero = -intercept / slope;
    const long double raw_at_maximum =
        (static_cast<long double>(extent - 1) - intercept) / slope;
    if (raw_at_zero < static_cast<long double>(std::numeric_limits<int>::min()) ||
        raw_at_zero > static_cast<long double>(std::numeric_limits<int>::max()) ||
        raw_at_maximum < static_cast<long double>(std::numeric_limits<int>::min()) ||
        raw_at_maximum > static_cast<long double>(std::numeric_limits<int>::max())) {
        set_diagnostic(diagnostic, std::string(axis_name) + " calibration is out of range");
        return std::nullopt;
    }
    return TouchAxisCalibration{static_cast<int>(std::llround(raw_at_zero)),
                                static_cast<int>(std::llround(raw_at_maximum))};
}

}  // namespace

TouchAxisCalibration default_touch_axis_calibration(AxisRange axis) {
    return {axis.minimum, axis.maximum};
}

bool touch_calibration_is_compatible(const TouchCalibration& calibration,
                                     AxisRange raw_x_axis, AxisRange raw_y_axis,
                                     int native_width, int native_height,
                                     std::string* diagnostic) {
    if (calibration.native_width != native_width || calibration.native_height != native_height ||
        native_width <= 1 || native_height <= 1) {
        set_diagnostic(diagnostic, "calibration is for a different display geometry");
        return false;
    }
    return validate_axis(calibration.x_axis, raw_x_axis, "X", diagnostic) &&
           validate_axis(calibration.y_axis, raw_y_axis, "Y", diagnostic);
}

std::optional<TouchCalibration> solve_touch_calibration(
    const std::vector<TouchCalibrationSample>& samples,
    AxisRange raw_x_axis, AxisRange raw_y_axis,
    int native_width, int native_height,
    std::string* diagnostic) {
    if (samples.size() != kRequiredSampleCount || native_width <= 1 || native_height <= 1) {
        set_diagnostic(diagnostic, "five valid calibration targets are required");
        return std::nullopt;
    }
    const auto x_axis = fit_axis(samples, true, native_width, "X", diagnostic);
    if (!x_axis.has_value()) {
        return std::nullopt;
    }
    const auto y_axis = fit_axis(samples, false, native_height, "Y", diagnostic);
    if (!y_axis.has_value()) {
        return std::nullopt;
    }
    TouchCalibration calibration{native_width, native_height, *x_axis, *y_axis};
    if (!touch_calibration_is_compatible(calibration, raw_x_axis, raw_y_axis,
                                         native_width, native_height, diagnostic)) {
        return std::nullopt;
    }
    return calibration;
}

std::optional<TouchCalibration> load_touch_calibration(const fs::path& path,
                                                        std::string* diagnostic) {
    std::error_code error;
    if (!fs::exists(path, error)) {
        if (error) {
            set_diagnostic(diagnostic, "unable to inspect calibration: " + error.message());
        }
        return std::nullopt;
    }
    const auto size = fs::file_size(path, error);
    if (error || size > kMaximumCalibrationFileBytes) {
        set_diagnostic(diagnostic, "calibration file is invalid");
        return std::nullopt;
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        set_diagnostic(diagnostic, "unable to read calibration file");
        return std::nullopt;
    }
    std::map<std::string, int> values;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t delimiter = line.find('=');
        if (delimiter == std::string::npos || delimiter == 0U ||
            delimiter + 1U == line.size()) {
            set_diagnostic(diagnostic, "calibration file contains an invalid line");
            return std::nullopt;
        }
        const std::string key = line.substr(0U, delimiter);
        if (std::find(kKeys.begin(), kKeys.end(), key) == kKeys.end() || values.count(key) != 0U) {
            set_diagnostic(diagnostic, "calibration file contains an unknown or repeated key");
            return std::nullopt;
        }
        int value = 0;
        if (!parse_integer(std::string_view(line).substr(delimiter + 1U), &value)) {
            set_diagnostic(diagnostic, "calibration file contains a non-integer value");
            return std::nullopt;
        }
        values.emplace(key, value);
    }
    if (values.size() != kKeys.size() || values["version"] != kCalibrationVersion) {
        set_diagnostic(diagnostic, "calibration file version is unsupported");
        return std::nullopt;
    }
    return TouchCalibration{values["native_width"], values["native_height"],
                            {values["x_raw_at_zero"], values["x_raw_at_maximum"]},
                            {values["y_raw_at_zero"], values["y_raw_at_maximum"]}};
}

bool save_touch_calibration(const fs::path& path, const TouchCalibration& calibration,
                            std::string* diagnostic) {
    if (path.empty() || path.parent_path().empty()) {
        set_diagnostic(diagnostic, "calibration path is invalid");
        return false;
    }
    const std::string content =
        "version=" + std::to_string(kCalibrationVersion) + "\n" +
        "native_width=" + std::to_string(calibration.native_width) + "\n" +
        "native_height=" + std::to_string(calibration.native_height) + "\n" +
        "x_raw_at_zero=" + std::to_string(calibration.x_axis.raw_at_zero) + "\n" +
        "x_raw_at_maximum=" + std::to_string(calibration.x_axis.raw_at_maximum) + "\n" +
        "y_raw_at_zero=" + std::to_string(calibration.y_axis.raw_at_zero) + "\n" +
        "y_raw_at_maximum=" + std::to_string(calibration.y_axis.raw_at_maximum) + "\n";
    const fs::path temporary = path.string() + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                          0640);
    if (fd < 0) {
        set_diagnostic(diagnostic, "unable to create calibration file: " + std::string(std::strerror(errno)));
        return false;
    }
    const bool written = write_all(fd, content) && ::fsync(fd) == 0;
    const int close_status = ::close(fd);
    if (!written || close_status != 0) {
        ::unlink(temporary.c_str());
        set_diagnostic(diagnostic, "unable to write calibration file");
        return false;
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        set_diagnostic(diagnostic, "unable to replace calibration file: " + std::string(std::strerror(errno)));
        return false;
    }
    const int parent_fd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_fd >= 0) {
        const int sync_status = ::fsync(parent_fd);
        ::close(parent_fd);
        if (sync_status != 0) {
            set_diagnostic(diagnostic, "unable to sync calibration directory");
            return false;
        }
    }
    return true;
}

bool remove_touch_calibration(const fs::path& path, std::string* diagnostic) {
    if (path.empty() || path.parent_path().empty()) {
        set_diagnostic(diagnostic, "calibration path is invalid");
        return false;
    }
    if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
        set_diagnostic(diagnostic,
                       "unable to remove calibration file: " + std::string(std::strerror(errno)));
        return false;
    }
    const int parent_fd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_fd < 0) {
        set_diagnostic(diagnostic,
                       "unable to open calibration directory: " + std::string(std::strerror(errno)));
        return false;
    }
    const int sync_status = ::fsync(parent_fd);
    const int close_status = ::close(parent_fd);
    if (sync_status != 0 || close_status != 0) {
        set_diagnostic(diagnostic, "unable to sync calibration directory");
        return false;
    }
    return true;
}

}  // namespace micropanel_touch::platform
