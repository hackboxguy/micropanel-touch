#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace micropanel_touch::platform {

// What the panel is and what it is running, read from exactly the files
// `ab-update status` reads.
//
// That constraint is the point of this type. Two answers to "what version is
// this?" that can disagree are worse than one, and the shell tool is the one
// an operator reaches for over SSH - so the screen must not compute its own
// version from somewhere else. Everything here is world-readable by design:
// the durable update state is root-only, and the commit service publishes this
// bounded summary for exactly this purpose.
struct AboutInfo {
    std::string version{"unknown"};
    std::string app_revision;
    std::string lvgl_revision;
    std::string panel_variant;
    std::string panel_profile;
    std::string slot{"unknown"};
    std::string update_state{"no update has run on this device"};
    // True only during the single boot after an update is installed, while the
    // commit service is still waiting out its health window. Restarting or
    // powering off in that window abandons the candidate, so screens that
    // offer either need to know.
    bool update_candidate_pending{false};
    std::string last_check{"never run"};
    std::string hostname;
};

// Strict KEY=value, the same shape the engine parses and never sources.
std::string manifest_value(const std::string& contents, const std::string& key);

// Wording shared with `ab-update status`, so a person reading the panel and a
// person reading the shell see the same sentence.
std::string describe_update_state(const std::string& published_state);

struct AboutPaths {
    std::filesystem::path image_manifest{
        "/opt/micropanel-touch/share/micropanel-touch/image-manifest.env"};
    std::filesystem::path update_status{"/run/micropanel-touch-update/status"};
    std::filesystem::path update_check{"/run/micropanel-touch-update/check"};
    std::filesystem::path kernel_command_line{"/proc/cmdline"};
    std::filesystem::path hostname{"/etc/hostname"};
};

AboutInfo read_about_info(const AboutPaths& paths = {});

// Display rows, in order. Formatting lives here so the wording is testable
// without a framebuffer.
std::vector<std::pair<std::string, std::string>> about_rows(const AboutInfo& info);

}  // namespace micropanel_touch::platform
