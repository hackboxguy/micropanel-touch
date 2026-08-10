#include "platform/DisplayBackend.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string read_text(const fs::path& path) {
    std::ifstream stream(path);
    std::string value;
    std::getline(stream, value);
    return trim(value);
}

std::optional<int> read_int(const fs::path& path) {
    std::ifstream stream(path);
    int value = 0;
    if (!(stream >> value)) {
        return std::nullopt;
    }
    return value;
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool contains_case_insensitive(std::string value, std::string needle) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.find(needle) != std::string::npos;
}

std::vector<fs::directory_entry> sorted_entries(const fs::path& path) {
    std::vector<fs::directory_entry> entries;
    std::error_code ec;
    if (!fs::is_directory(path, ec)) {
        return entries;
    }
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (!ec) {
            entries.push_back(entry);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.path().filename().string() < right.path().filename().string();
    });
    return entries;
}

bool is_card_name(const std::string& value) {
    if (!starts_with(value, "card") || value.size() == 4) {
        return false;
    }
    return std::all_of(value.begin() + 4, value.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

bool path_has_component(const fs::path& path, const std::string& component) {
    return std::any_of(path.begin(), path.end(), [&component](const fs::path& part) {
        return part == component;
    });
}

bool is_path_prefix(const fs::path& prefix, const fs::path& path) {
    auto prefix_part = prefix.begin();
    auto path_part = path.begin();
    while (prefix_part != prefix.end() && path_part != path.end()) {
        if (*prefix_part != *path_part) {
            return false;
        }
        ++prefix_part;
        ++path_part;
    }
    return prefix_part == prefix.end();
}

std::optional<fs::path> framebuffer_for_connector(const SystemPaths& paths,
                                                   const std::string& connector) {
    std::error_code connector_ec;
    const fs::path connector_device =
        fs::weakly_canonical(paths.drm_class / connector, connector_ec);
    for (const auto& entry : sorted_entries(paths.graphics_class)) {
        const std::string fb_name = entry.path().filename().string();
        if (!starts_with(fb_name, "fb")) {
            continue;
        }

        std::error_code ec;
        const fs::path device = fs::weakly_canonical(entry.path() / "device", ec);
        if (!ec && (device.filename() == connector || path_has_component(device, connector))) {
            return paths.device_root / fb_name;
        }
        // The PiScreen DRM driver exposes graphics/fbN and drm/cardN-SPI-1 as
        // siblings under one SPI device.  The framebuffer's canonical device
        // path is therefore an ancestor of the connector's canonical path.
        if (!ec && !connector_ec && is_path_prefix(device, connector_device)) {
            return paths.device_root / fb_name;
        }
    }
    return std::nullopt;
}

std::vector<BacklightInfo> entries_to_backlights(const fs::path& root, const char* kind) {
    std::vector<BacklightInfo> result;
    for (const auto& entry : sorted_entries(root)) {
        BacklightInfo info;
        info.directory = entry.path();
        info.kind = kind;
        info.brightness = read_int(entry.path() / "brightness");
        info.max_brightness = read_int(entry.path() / "max_brightness");
        result.push_back(std::move(info));
    }
    return result;
}

}  // namespace

std::vector<DrmCardInfo> DisplayBackend::enumerate_drm(const SystemPaths& paths) {
    std::vector<DrmCardInfo> cards;
    for (const auto& entry : sorted_entries(paths.dri_by_path)) {
        std::error_code ec;
        if (!entry.is_symlink(ec) || ec) {
            continue;
        }
        const fs::path resolved = fs::weakly_canonical(entry.path(), ec);
        if (ec) {
            continue;
        }
        const std::string card_name = resolved.filename().string();
        if (!is_card_name(card_name)) {
            continue;
        }

        DrmCardInfo card;
        card.by_path = entry.path();
        card.card_name = card_name;
        const std::string connector_prefix = card_name + "-";
        for (const auto& connector : sorted_entries(paths.drm_class)) {
            const std::string connector_name = connector.path().filename().string();
            if (!starts_with(connector_name, connector_prefix)) {
                continue;
            }
            card.connectors.push_back({connector_name, read_text(connector.path() / "status")});
        }
        cards.push_back(std::move(card));
    }
    return cards;
}

std::optional<DisplayTarget> DisplayBackend::discover(const SystemPaths& paths,
                                                       std::string* diagnostic) {
    const auto cards = enumerate_drm(paths);
    if (cards.empty()) {
        if (diagnostic != nullptr) {
            *diagnostic = "No DRM card entries found in " + paths.dri_by_path.string();
        }
        return std::nullopt;
    }

    std::vector<DrmCardInfo> ordered = cards;
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return contains_case_insensitive(left.by_path.filename().string(), "spi") &&
               !contains_case_insensitive(right.by_path.filename().string(), "spi");
    });

    for (const auto& card : ordered) {
        for (const auto& connector : card.connectors) {
            if (connector.status != "connected") {
                continue;
            }
            const auto framebuffer = framebuffer_for_connector(paths, connector.name);
            if (!framebuffer.has_value()) {
                continue;
            }
            return DisplayTarget{card.by_path, card.card_name, connector.name, *framebuffer};
        }
    }

    if (diagnostic != nullptr) {
        *diagnostic = "No connected DRM connector could be mapped to a framebuffer";
    }
    return std::nullopt;
}

std::optional<DisplayTarget> DisplayBackend::discover(std::string* diagnostic) {
    return discover(SystemPaths{}, diagnostic);
}

std::vector<BacklightInfo> DisplayBackend::enumerate_backlights(const SystemPaths& paths) {
    auto result = entries_to_backlights(paths.backlight_class, "backlight");
    auto leds = entries_to_backlights(paths.leds_class, "led");
    result.insert(result.end(), leds.begin(), leds.end());
    return result;
}

std::string DisplayBackend::format_probe(const SystemPaths& paths) {
    std::ostringstream output;
    const auto cards = enumerate_drm(paths);
    output << "DRM by-path entries: " << cards.size() << '\n';
    for (const auto& card : cards) {
        output << "  " << card.by_path << " -> " << card.card_name << '\n';
        for (const auto& connector : card.connectors) {
            output << "    " << connector.name << ": " << connector.status;
            const auto framebuffer = framebuffer_for_connector(paths, connector.name);
            if (framebuffer.has_value()) {
                output << " -> " << *framebuffer;
            }
            output << '\n';
        }
    }
    const auto backlights = enumerate_backlights(paths);
    output << "Backlight/LED entries: " << backlights.size() << '\n';
    for (const auto& backlight : backlights) {
        output << "  [" << backlight.kind << "] " << backlight.directory;
        if (backlight.brightness.has_value()) {
            output << " brightness=" << *backlight.brightness;
        }
        if (backlight.max_brightness.has_value()) {
            output << '/' << *backlight.max_brightness;
        }
        output << '\n';
    }
    return output.str();
}

}  // namespace micropanel_touch::platform
