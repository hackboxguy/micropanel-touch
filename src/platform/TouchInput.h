#pragma once

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <lvgl.h>

namespace micropanel_touch::platform {

struct AxisRange {
    int minimum{0};
    int maximum{4095};
};

struct TouchPoint {
    int x{0};
    int y{0};
};

class TouchMapper {
public:
    TouchMapper(AxisRange x_axis, AxisRange y_axis, int width, int height);
    TouchPoint map(int raw_x, int raw_y) const;

private:
    static int scale(int raw, AxisRange axis, int extent);

    AxisRange x_axis_;
    AxisRange y_axis_;
    int width_;
    int height_;
};

/**
 * ADS7846 reports BTN_TOUCH before the first coordinate packet.  This filter
 * deliberately withholds a pressed LVGL contact until a complete, non-zero
 * pressure sample arrives after the press.
 */
class TouchContactFilter {
public:
    TouchContactFilter(AxisRange x_axis, AxisRange y_axis, int width, int height);

    void handle_event(unsigned short type, unsigned short code, int value);
    bool pressed() const;
    TouchPoint point() const;

private:
    TouchMapper mapper_;
    bool touch_down_{false};
    bool have_x_{false};
    bool have_y_{false};
    bool have_pressure_{false};
    bool pressed_{false};
    int raw_x_{0};
    int raw_y_{0};
    int pressure_{0};
};

struct TouchReport {
    TouchPoint point;
    bool pressed{false};
};

/**
 * Preserve every completed kernel report until LVGL has consumed it. A single
 * evdev read can contain a complete short tap, so reducing it to the final
 * state would make LVGL see only the release.
 */
class TouchReportBuffer {
public:
    TouchReportBuffer(AxisRange x_axis, AxisRange y_axis, int width, int height);

    void handle_event(unsigned short type, unsigned short code, int value);
    std::optional<TouchReport> next_report();
    TouchReport current() const;
    bool has_pending() const;

private:
    TouchContactFilter filter_;
    TouchReport current_{};
    std::deque<TouchReport> reports_;
};

struct TouchDeviceInfo {
    std::filesystem::path path;
    std::string name;
    AxisRange x_axis;
    AxisRange y_axis;
    AxisRange pressure_axis;
};

class TouchInput {
public:
    ~TouchInput();
    TouchInput(const TouchInput&) = delete;
    TouchInput& operator=(const TouchInput&) = delete;

    static std::vector<TouchDeviceInfo> enumerate(
        const std::filesystem::path& input_root = "/dev/input");
    static std::unique_ptr<TouchInput> open_auto(std::string* diagnostic = nullptr);
    static std::unique_ptr<TouchInput> open(const std::filesystem::path& path,
                                            std::string* diagnostic = nullptr);

    void set_display_size(int width, int height);
    void attach_to_lvgl();
    void read(lv_indev_data_t* data);
    const TouchDeviceInfo& device() const;

private:
    TouchInput(int fd, TouchDeviceInfo device, int width, int height);
    static void read_callback(lv_indev_t* indev, lv_indev_data_t* data);
    void drain_events();

    int fd_{-1};
    TouchDeviceInfo device_;
    TouchReportBuffer reports_;
    lv_indev_t* indev_{nullptr};
};

}  // namespace micropanel_touch::platform
