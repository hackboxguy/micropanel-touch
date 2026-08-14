#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/TouchInput.h"

#include <cassert>
#include <linux/input.h>

using micropanel_touch::platform::AxisRange;
using micropanel_touch::platform::TouchContactFilter;
using micropanel_touch::platform::TouchMapper;
using micropanel_touch::platform::TouchReportBuffer;
using micropanel_touch::platform::TouchTechnology;

int main() {
    const TouchMapper mapper({100, 4100}, {200, 4200}, 480, 320);
    assert(mapper.map(100, 200).x == 0);
    assert(mapper.map(100, 200).y == 0);
    assert(mapper.map(4100, 4200).x == 479);
    assert(mapper.map(4100, 4200).y == 319);
    assert(mapper.map(2100, 2200).x == 240);
    assert(mapper.map(2100, 2200).y == 160);

    TouchContactFilter filter({0, 4095}, {0, 4095}, 480, 320);
    filter.handle_event(EV_KEY, BTN_TOUCH, 1);
    filter.handle_event(EV_SYN, SYN_REPORT, 0);
    assert(!filter.pressed());

    filter.handle_event(EV_ABS, ABS_X, 4095);
    filter.handle_event(EV_ABS, ABS_Y, 4095);
    filter.handle_event(EV_ABS, ABS_PRESSURE, 80);
    filter.handle_event(EV_SYN, SYN_REPORT, 0);
    assert(filter.pressed());
    assert(filter.point().x == 479);
    assert(filter.point().y == 319);

    filter.handle_event(EV_KEY, BTN_TOUCH, 0);
    filter.handle_event(EV_SYN, SYN_REPORT, 0);
    assert(!filter.pressed());

    // A short physical tap can be fully queued before the next LVGL input
    // read. Preserve its press and release reports in order.
    TouchReportBuffer reports({0, 4095}, {0, 4095}, 480, 320);
    reports.handle_event(EV_KEY, BTN_TOUCH, 1);
    reports.handle_event(EV_ABS, ABS_X, 2048);
    reports.handle_event(EV_ABS, ABS_Y, 2048);
    reports.handle_event(EV_ABS, ABS_PRESSURE, 100);
    reports.handle_event(EV_SYN, SYN_REPORT, 0);
    reports.handle_event(EV_KEY, BTN_TOUCH, 0);
    reports.handle_event(EV_ABS, ABS_PRESSURE, 0);
    reports.handle_event(EV_SYN, SYN_REPORT, 0);
    const auto press = reports.next_report();
    assert(press.has_value() && press->pressed);
    const auto release = reports.next_report();
    assert(release.has_value() && !release->pressed);
    assert(!reports.has_pending());

    // Goodix/FT5x06-class controllers report type-B multitouch contacts. The
    // UI consumes one primary contact and releases it rather than jumping to
    // another finger when the primary finger lifts.
    TouchContactFilter multitouch_filter({0, 4095}, {0, 4095}, 480, 320,
                                          TouchTechnology::capacitive_multitouch);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_SLOT, 0);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_TRACKING_ID, 10);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_POSITION_X, 2048);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_POSITION_Y, 1024);
    multitouch_filter.handle_event(EV_SYN, SYN_REPORT, 0);
    assert(multitouch_filter.pressed());
    assert(multitouch_filter.point().x == 240);
    assert(multitouch_filter.point().y == 80);

    multitouch_filter.handle_event(EV_ABS, ABS_MT_SLOT, 1);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_TRACKING_ID, 11);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_POSITION_X, 4095);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_POSITION_Y, 4095);
    multitouch_filter.handle_event(EV_SYN, SYN_REPORT, 0);
    // Slot zero retains ownership while it remains down.
    assert(multitouch_filter.point().x == 240);
    assert(multitouch_filter.point().y == 80);

    multitouch_filter.handle_event(EV_ABS, ABS_MT_SLOT, 0);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_TRACKING_ID, -1);
    multitouch_filter.handle_event(EV_SYN, SYN_REPORT, 0);
    assert(!multitouch_filter.pressed());

    // Keeping the second finger down or moving it cannot start a new press
    // until every contact from the prior gesture has lifted.
    multitouch_filter.handle_event(EV_ABS, ABS_MT_SLOT, 1);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_POSITION_X, 0);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_POSITION_Y, 0);
    multitouch_filter.handle_event(EV_SYN, SYN_REPORT, 0);
    assert(!multitouch_filter.pressed());

    multitouch_filter.handle_event(EV_ABS, ABS_MT_SLOT, 1);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_TRACKING_ID, -1);
    multitouch_filter.handle_event(EV_SYN, SYN_REPORT, 0);
    assert(!multitouch_filter.pressed());

    // A fresh contact after the all-up boundary becomes the next primary.
    multitouch_filter.handle_event(EV_ABS, ABS_MT_TRACKING_ID, 12);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_POSITION_X, 4095);
    multitouch_filter.handle_event(EV_ABS, ABS_MT_POSITION_Y, 4095);
    multitouch_filter.handle_event(EV_SYN, SYN_REPORT, 0);
    assert(multitouch_filter.pressed());
    assert(multitouch_filter.point().x == 479);
    assert(multitouch_filter.point().y == 319);

    TouchReportBuffer bounded_reports({0, 4095}, {0, 4095}, 480, 320);
    bounded_reports.handle_event(EV_KEY, BTN_TOUCH, 1);
    for (int value = 0; value < 65; ++value) {
        bounded_reports.handle_event(EV_ABS, ABS_X, value);
        bounded_reports.handle_event(EV_ABS, ABS_Y, value);
        bounded_reports.handle_event(EV_ABS, ABS_PRESSURE, 100);
        bounded_reports.handle_event(EV_SYN, SYN_REPORT, 0);
    }
    // Queue overflow resets to a release/press pair instead of retaining an
    // unbounded stale gesture history.
    const auto bounded_release = bounded_reports.next_report();
    assert(bounded_release.has_value() && !bounded_release->pressed);
    const auto bounded_press = bounded_reports.next_report();
    assert(bounded_press.has_value() && bounded_press->pressed);
    assert(!bounded_reports.has_pending());
    return 0;
}
