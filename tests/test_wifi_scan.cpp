#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/WifiScan.h"

#include <cassert>

int main() {
    const auto access_points = micropanel_touch::platform::parse_nmcli_wifi_list(
        "*:Lab\\:Network:AA\\:BB\\:CC\\:DD\\:EE\\:FF:83:WPA2\\:WPA3\n"
        ":Guest\\\\WiFi:11\\:22\\:33\\:44\\:55\\:66:42:\n");

    assert(access_points.size() == 2U);
    assert(access_points[0].active);
    assert(access_points[0].ssid == "Lab:Network");
    assert(access_points[0].bssid == "AA:BB:CC:DD:EE:FF");
    assert(access_points[0].signal_percent == 83U);
    assert(access_points[0].security == "WPA2:WPA3");
    assert(!access_points[1].active);
    assert(access_points[1].ssid == "Guest\\WiFi");
    assert(access_points[1].security.empty());
    return 0;
}
