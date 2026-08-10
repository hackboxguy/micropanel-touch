#include "platform/TouchInput.h"

#include <cassert>
#include <linux/input.h>

using micropanel_touch::platform::AxisRange;
using micropanel_touch::platform::TouchContactFilter;
using micropanel_touch::platform::TouchMapper;

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
    return 0;
}
