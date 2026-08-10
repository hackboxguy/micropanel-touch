#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/DisplayBackend.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using micropanel_touch::platform::DisplayBackend;
using micropanel_touch::platform::SystemPaths;

namespace {

void write_file(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << value;
}

}  // namespace

int main() {
    char pattern[] = "/tmp/micropanel-touch-display-test-XXXXXX";
    const char* const root_value = mkdtemp(pattern);
    assert(root_value != nullptr);
    const fs::path root(root_value);

    const fs::path dri = root / "dri";
    fs::create_directories(dri / "by-path");
    write_file(dri / "card0", "");
    write_file(dri / "card1", "");
    fs::create_symlink("../card0", dri / "by-path/platform-gpu-card");
    fs::create_symlink("../card1", dri / "by-path/platform-fe204000.spi-cs-0-card");

    const fs::path drm = root / "drm";
    const fs::path devices_tree = root / "devices";
    fs::create_directories(drm);
    write_file(devices_tree / "gpu/drm/card0/card0-HDMI-A-1/status", "connected\n");
    write_file(devices_tree / "spi0.0/drm/card1/card1-SPI-1/status", "connected\n");
    fs::create_symlink("../devices/gpu/drm/card0/card0-HDMI-A-1", drm / "card0-HDMI-A-1");
    fs::create_symlink("../devices/spi0.0/drm/card1/card1-SPI-1", drm / "card1-SPI-1");

    const fs::path graphics = root / "graphics";
    fs::create_directories(graphics / "fb0");
    fs::create_directories(graphics / "fb1");
    fs::create_symlink("../../devices/gpu", graphics / "fb0/device");
    fs::create_symlink("../../devices/spi0.0", graphics / "fb1/device");

    const fs::path devices = root / "dev";
    write_file(devices / "fb0", "");
    write_file(devices / "fb1", "");

    SystemPaths paths;
    paths.dri_by_path = dri / "by-path";
    paths.drm_class = drm;
    paths.graphics_class = graphics;
    paths.device_root = devices;
    paths.backlight_class = root / "backlight";
    paths.leds_class = root / "leds";

    std::string diagnostic;
    const auto target = DisplayBackend::discover(paths, &diagnostic);
    assert(target.has_value());
    assert(target->drm_card == "card1");
    assert(target->connector == "card1-SPI-1");
    assert(target->framebuffer == devices / "fb1");

    fs::remove_all(root);
    return 0;
}
