#include "platform/TouchInput.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

constexpr std::size_t kBitsPerLong = sizeof(unsigned long) * 8U;
constexpr std::size_t kMaxQueuedReports = 64U;

template <std::size_t Bits>
using BitArray = std::array<unsigned long, (Bits + kBitsPerLong - 1U) / kBitsPerLong>;

bool bit_is_set(int bit, const unsigned long* bits) {
    return (bits[static_cast<std::size_t>(bit) / kBitsPerLong] &
            (1UL << (static_cast<std::size_t>(bit) % kBitsPerLong))) != 0U;
}

bool has_abs_axis(int fd, int axis) {
    BitArray<ABS_MAX + 1> bits{};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits.data()) < 0) {
        return false;
    }
    return bit_is_set(axis, bits.data());
}

std::optional<AxisRange> axis_range(int fd, int axis) {
    input_absinfo info{};
    if (ioctl(fd, EVIOCGABS(axis), &info) < 0) {
        return std::nullopt;
    }
    return AxisRange{info.minimum, info.maximum};
}

std::optional<TouchDeviceInfo> inspect_device(const fs::path& path) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return std::nullopt;
    }

    BitArray<EV_MAX + 1> event_bits{};
    BitArray<KEY_MAX + 1> key_bits{};
    const bool has_event_bits = ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits.data()) >= 0;
    const bool has_key_bits = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits.data()) >= 0;
    const bool signature_matches = has_event_bits && has_key_bits &&
        bit_is_set(EV_ABS, event_bits.data()) && bit_is_set(EV_KEY, event_bits.data()) &&
        bit_is_set(BTN_TOUCH, key_bits.data()) && has_abs_axis(fd, ABS_X) && has_abs_axis(fd, ABS_Y) &&
        !has_abs_axis(fd, ABS_MT_POSITION_X) && !has_abs_axis(fd, ABS_MT_POSITION_Y);

    if (!signature_matches) {
        close(fd);
        return std::nullopt;
    }

    const auto x_axis = axis_range(fd, ABS_X);
    const auto y_axis = axis_range(fd, ABS_Y);
    const auto pressure_axis = axis_range(fd, ABS_PRESSURE);
    if (!x_axis.has_value() || !y_axis.has_value() || !pressure_axis.has_value()) {
        close(fd);
        return std::nullopt;
    }

    std::array<char, 256> name{};
    if (ioctl(fd, EVIOCGNAME(name.size()), name.data()) < 0) {
        std::strncpy(name.data(), "unnamed touch device", name.size() - 1U);
    }
    close(fd);
    return TouchDeviceInfo{path, name.data(), *x_axis, *y_axis, *pressure_axis};
}

bool is_event_node(const fs::directory_entry& entry) {
    const std::string filename = entry.path().filename().string();
    return filename.size() > 5U && filename.compare(0, 5, "event") == 0 &&
        std::all_of(filename.begin() + 5, filename.end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        });
}

}  // namespace

TouchMapper::TouchMapper(AxisRange x_axis, AxisRange y_axis, int width, int height)
    : TouchMapper({default_touch_axis_calibration(x_axis), default_touch_axis_calibration(y_axis)},
                  width, height) {}

TouchMapper::TouchMapper(TouchAxisMappings axes, int width, int height)
    : x_axis_(axes.x_axis), y_axis_(axes.y_axis), width_(width), height_(height) {}

int TouchMapper::scale(int raw, TouchAxisCalibration axis, int extent) {
    if (extent <= 1 || axis.raw_at_maximum == axis.raw_at_zero) {
        return 0;
    }
    const int low = std::min(axis.raw_at_zero, axis.raw_at_maximum);
    const int high = std::max(axis.raw_at_zero, axis.raw_at_maximum);
    const long long clamped = std::clamp(raw, low, high);
    const long double coordinate =
        (static_cast<long double>(clamped - axis.raw_at_zero) * (extent - 1)) /
        static_cast<long double>(axis.raw_at_maximum - axis.raw_at_zero);
    return std::clamp(static_cast<int>(std::llround(coordinate)), 0, extent - 1);
}

TouchPoint TouchMapper::map(int raw_x, int raw_y) const {
    return {scale(raw_x, x_axis_, width_), scale(raw_y, y_axis_, height_)};
}

TouchContactFilter::TouchContactFilter(AxisRange x_axis, AxisRange y_axis, int width, int height)
    : TouchContactFilter({default_touch_axis_calibration(x_axis),
                          default_touch_axis_calibration(y_axis)}, width, height) {}

TouchContactFilter::TouchContactFilter(TouchAxisMappings axes, int width, int height)
    : mapper_(axes, width, height) {}

void TouchContactFilter::handle_event(unsigned short type, unsigned short code, int value) {
    if (type == EV_KEY && code == BTN_TOUCH) {
        touch_down_ = value != 0;
        if (touch_down_) {
            have_x_ = false;
            have_y_ = false;
            have_pressure_ = false;
            pressed_ = false;
        } else {
            pressed_ = false;
        }
        return;
    }
    if (type == EV_ABS) {
        if (code == ABS_X) {
            raw_x_ = value;
            have_x_ = true;
        } else if (code == ABS_Y) {
            raw_y_ = value;
            have_y_ = true;
        } else if (code == ABS_PRESSURE) {
            pressure_ = value;
            have_pressure_ = true;
        }
        return;
    }
    if (type == EV_SYN && code == SYN_REPORT) {
        pressed_ = touch_down_ && have_x_ && have_y_ && have_pressure_ && pressure_ > 0;
    }
}

bool TouchContactFilter::pressed() const {
    return pressed_;
}

TouchPoint TouchContactFilter::point() const {
    return mapper_.map(raw_x_, raw_y_);
}

TouchPoint TouchContactFilter::raw_point() const {
    return {raw_x_, raw_y_};
}

TouchReportBuffer::TouchReportBuffer(AxisRange x_axis, AxisRange y_axis, int width, int height)
    : TouchReportBuffer({default_touch_axis_calibration(x_axis),
                         default_touch_axis_calibration(y_axis)}, width, height) {}

TouchReportBuffer::TouchReportBuffer(TouchAxisMappings axes, int width, int height)
    : filter_(axes, width, height) {}

void TouchReportBuffer::handle_event(unsigned short type, unsigned short code, int value) {
    filter_.handle_event(type, code, value);
    if (type != EV_SYN || code != SYN_REPORT) {
        return;
    }
    current_ = {filter_.point(), filter_.raw_point(), filter_.pressed()};
    if (reports_.size() >= kMaxQueuedReports) {
        // A paused UI must not accumulate unbounded evdev history. Drop the
        // stale gesture and restart from a coherent release/press boundary.
        reports_.clear();
        if (current_.pressed) {
            reports_.push_back({current_.point, current_.raw_point, false});
        }
    }
    reports_.push_back(current_);
}

std::optional<TouchReport> TouchReportBuffer::next_report() {
    if (reports_.empty()) {
        return std::nullopt;
    }
    TouchReport report = reports_.front();
    reports_.pop_front();
    return report;
}

TouchReport TouchReportBuffer::current() const {
    return current_;
}

bool TouchReportBuffer::has_pending() const {
    return !reports_.empty();
}

TouchInput::TouchInput(int fd, TouchDeviceInfo device, int width, int height)
    : fd_(fd), device_(std::move(device)), reports_(device_.x_axis, device_.y_axis, width, height),
      display_width_(width), display_height_(height) {}

TouchInput::~TouchInput() {
    if (indev_ != nullptr) {
        lv_indev_delete(indev_);
        indev_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

std::vector<TouchDeviceInfo> TouchInput::enumerate(const fs::path& input_root) {
    std::vector<TouchDeviceInfo> devices;
    std::error_code ec;
    if (!fs::is_directory(input_root, ec)) {
        return devices;
    }
    for (const auto& entry : fs::directory_iterator(input_root, ec)) {
        if (ec || !is_event_node(entry)) {
            continue;
        }
        if (const auto device = inspect_device(entry.path()); device.has_value()) {
            devices.push_back(*device);
        }
    }
    std::sort(devices.begin(), devices.end(), [](const auto& left, const auto& right) {
        return left.path.string() < right.path.string();
    });
    return devices;
}

std::unique_ptr<TouchInput> TouchInput::open_auto(std::string* diagnostic) {
    const auto devices = enumerate();
    if (devices.empty()) {
        if (diagnostic != nullptr) {
            *diagnostic = "No single-touch ADS7846-compatible event device found";
        }
        return nullptr;
    }
    return open(devices.front().path, diagnostic);
}

std::unique_ptr<TouchInput> TouchInput::open(const fs::path& path, std::string* diagnostic) {
    const auto device = inspect_device(path);
    if (!device.has_value()) {
        if (diagnostic != nullptr) {
            *diagnostic = path.string() + " is not an accessible ADS7846-compatible touch device";
        }
        return nullptr;
    }
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (diagnostic != nullptr) {
            *diagnostic = "Unable to open " + path.string() + ": " + std::strerror(errno);
        }
        return nullptr;
    }
    return std::unique_ptr<TouchInput>(new TouchInput(fd, *device, 480, 320));
}

void TouchInput::set_display_size(int width, int height) {
    display_width_ = width;
    display_height_ = height;
    reset_reports();
}

void TouchInput::set_calibration(const TouchCalibration& calibration) {
    calibration_ = calibration;
    reset_reports();
}

void TouchInput::clear_calibration() {
    calibration_.reset();
    reset_reports();
}

void TouchInput::set_raw_touch_callback(RawTouchCallback callback) {
    raw_touch_callback_ = std::move(callback);
}

void TouchInput::reset_reports() {
    if (calibration_.has_value()) {
        reports_ = TouchReportBuffer({calibration_->x_axis, calibration_->y_axis},
                                     display_width_, display_height_);
    } else {
        reports_ = TouchReportBuffer(device_.x_axis, device_.y_axis,
                                     display_width_, display_height_);
    }
    previous_report_pressed_ = false;
}

void TouchInput::attach_to_lvgl() {
    indev_ = lv_indev_create();
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(indev_, this);
    lv_indev_set_read_cb(indev_, read_callback);
}

void TouchInput::read_callback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* input = static_cast<TouchInput*>(lv_indev_get_user_data(indev));
    input->read(data);
}

void TouchInput::drain_events() {
    std::array<input_event, 16> events{};
    while (true) {
        const ssize_t bytes = ::read(fd_, events.data(), sizeof(events));
        if (bytes <= 0) {
            return;
        }
        const std::size_t count = static_cast<std::size_t>(bytes) / sizeof(input_event);
        for (std::size_t index = 0; index < count; ++index) {
            reports_.handle_event(events[index].type, events[index].code, events[index].value);
        }
    }
}

void TouchInput::read(lv_indev_data_t* data) {
    drain_events();
    const TouchReport report = reports_.next_report().value_or(reports_.current());
    if (report.pressed && !previous_report_pressed_ && raw_touch_callback_) {
        raw_touch_callback_({report.raw_point, report.point});
    }
    previous_report_pressed_ = report.pressed;
    data->point.x = report.point.x;
    data->point.y = report.point.y;
    data->state = report.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = reports_.has_pending();
}

const TouchDeviceInfo& TouchInput::device() const {
    return device_;
}

}  // namespace micropanel_touch::platform
