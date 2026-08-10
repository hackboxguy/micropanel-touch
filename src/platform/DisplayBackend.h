#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace micropanel_touch::platform {

struct SystemPaths {
    std::filesystem::path dri_by_path{ "/dev/dri/by-path" };
    std::filesystem::path drm_class{ "/sys/class/drm" };
    std::filesystem::path graphics_class{ "/sys/class/graphics" };
    std::filesystem::path device_root{ "/dev" };
    std::filesystem::path backlight_class{ "/sys/class/backlight" };
    std::filesystem::path leds_class{ "/sys/class/leds" };
};

struct ConnectorInfo {
    std::string name;
    std::string status;
};

struct DrmCardInfo {
    std::filesystem::path by_path;
    std::string card_name;
    std::vector<ConnectorInfo> connectors;
};

struct DisplayTarget {
    std::filesystem::path drm_by_path;
    std::string drm_card;
    std::string connector;
    std::filesystem::path framebuffer;
};

struct BacklightInfo {
    std::filesystem::path directory;
    std::string kind;
    std::optional<int> brightness;
    std::optional<int> max_brightness;
};

/**
 * Resolves the configured display backend rather than pretending a write-only
 * SPI panel can be physically hot-plug detected.  The discovery result ties a
 * stable DRM by-path card to a DRM connector and then to its /dev/fbN node.
 */
class DisplayBackend {
public:
    static std::vector<DrmCardInfo> enumerate_drm(const SystemPaths& paths = {});
    static std::optional<DisplayTarget> discover(const SystemPaths& paths,
                                                 std::string* diagnostic = nullptr);
    static std::optional<DisplayTarget> discover(std::string* diagnostic = nullptr);
    static std::vector<BacklightInfo> enumerate_backlights(const SystemPaths& paths = {});
    static std::string format_probe(const SystemPaths& paths = {});
};

}  // namespace micropanel_touch::platform
