#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/StaticIpSettings.h"

#include <cassert>

using micropanel_touch::core::StaticIpSettings;
using micropanel_touch::core::validate_static_ipv4;

int main() {
    assert(validate_static_ipv4({"192.168.1.50", "24", "192.168.1.1"}).valid);
    assert(validate_static_ipv4({"10.0.0.2", "0", "10.0.0.1"}).valid);

    assert(!validate_static_ipv4({"192.168.1", "24", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.50.", "24", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.256", "24", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.50", "33", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.50", "twenty-four", "192.168.1.1"}).valid);
    assert(!validate_static_ipv4({"192.168.1.50", "24", "192.168.1"}).valid);
    return 0;
}
