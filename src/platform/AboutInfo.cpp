#include "platform/AboutInfo.h"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace micropanel_touch::platform {
namespace {

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream.good()) {
        return {};
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

std::string trim(const std::string& value) {
    const std::string::size_type first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::string::size_type last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

// A git revision on a 320 px panel is a wall of hex that says nothing. Seven
// characters is what every tool that shows a revision to a human shows.
std::string short_revision(const std::string& revision) {
    return revision.size() > 7U ? revision.substr(0U, 7U) : revision;
}

}  // namespace

std::string manifest_value(const std::string& contents, const std::string& key) {
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        const std::string::size_type separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        if (trim(line.substr(0U, separator)) == key) {
            return trim(line.substr(separator + 1U));
        }
    }
    return {};
}

std::string describe_update_state(const std::string& published_state) {
    if (published_state == "committed") {
        return "committed";
    }
    if (published_state == "candidate-armed") {
        return "candidate boot pending";
    }
    if (published_state == "fallback") {
        return "candidate abandoned; committed slot retained";
    }
    if (published_state.empty()) {
        return "no update has run on this device";
    }
    return published_state;
}

AboutInfo read_about_info(const AboutPaths& paths) {
    AboutInfo info;

    const std::string manifest = read_file(paths.image_manifest);
    if (const std::string version = manifest_value(manifest, "IMAGE_VERSION"); !version.empty()) {
        info.version = version;
    }
    info.app_revision = manifest_value(manifest, "MICROPANEL_TOUCH_REVISION");
    if (info.app_revision.empty()) {
        info.app_revision = manifest_value(manifest, "AB_APP_REVISION");
    }
    info.lvgl_revision = manifest_value(manifest, "LVGL_REVISION");
    info.panel_variant = manifest_value(manifest, "PANEL_VARIANT");
    info.panel_profile = manifest_value(manifest, "PANEL_PROFILE");

    // The running slot comes from the kernel command line rather than from the
    // selector, because the selector is root-only and this screen is not.
    const std::string command_line = read_file(paths.kernel_command_line);
    if (command_line.find("root=LABEL=MP_ROOT_A") != std::string::npos) {
        info.slot = "A";
    } else if (command_line.find("root=LABEL=MP_ROOT_B") != std::string::npos) {
        info.slot = "B";
    }

    info.update_state = describe_update_state(manifest_value(read_file(paths.update_status), "state"));

    const std::string check = read_file(paths.update_check);
    if (const std::string check_state = manifest_value(check, "state"); !check_state.empty()) {
        const std::string check_version = manifest_value(check, "version");
        info.last_check = check_version.empty() ? check_state
                                                : check_state + " (" + check_version + ")";
    }

    info.hostname = trim(read_file(paths.hostname));

    return info;
}

std::vector<std::pair<std::string, std::string>> about_rows(const AboutInfo& info) {
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("Version", info.version);
    rows.emplace_back("Slot", info.slot);
    if (!info.app_revision.empty()) {
        rows.emplace_back("App", short_revision(info.app_revision));
    }
    if (!info.panel_variant.empty()) {
        rows.emplace_back("Panel", info.panel_variant);
    }
    if (!info.hostname.empty()) {
        rows.emplace_back("Host", info.hostname);
    }
    rows.emplace_back("Update", info.update_state);
    rows.emplace_back("Last check", info.last_check);
    return rows;
}

}  // namespace micropanel_touch::platform
