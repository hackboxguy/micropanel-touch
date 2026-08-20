#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/AboutInfo.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using micropanel_touch::platform::about_rows;
using micropanel_touch::platform::AboutInfo;
using micropanel_touch::platform::AboutPaths;
using micropanel_touch::platform::describe_update_state;
using micropanel_touch::platform::manifest_value;
using micropanel_touch::platform::read_about_info;

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    assert(stream.good());
    stream << contents;
}

std::string row_value(const AboutInfo& info, const std::string& label) {
    for (const auto& row : about_rows(info)) {
        if (row.first == label) {
            return row.second;
        }
    }
    return "<missing>";
}

bool has_row(const AboutInfo& info, const std::string& label) {
    for (const auto& row : about_rows(info)) {
        if (row.first == label) {
            return true;
        }
    }
    return false;
}

void test_manifest_parsing() {
    const std::string manifest =
        "# a comment, and a line with no separator\n"
        "MICROPANEL_TOUCH_REVISION=5b3d9b3263ac2cdc2299d32bd7da319d4f5334f8\n"
        "IMAGE_VERSION=00.39\n"
        "EMPTY=\n";
    assert(manifest_value(manifest, "IMAGE_VERSION") == "00.39");
    assert(manifest_value(manifest, "EMPTY").empty());
    assert(manifest_value(manifest, "ABSENT").empty());
    // A key that is a prefix of another must not match it: IMAGE_VERSION and
    // IMAGE_VERSION_SUFFIX are different keys.
    assert(manifest_value("IMAGE_VERSION_SUFFIX=x\n", "IMAGE_VERSION").empty());
    // Values may contain '='; only the first one separates.
    assert(manifest_value("URL=https://example.invalid/?a=b\n", "URL") ==
           "https://example.invalid/?a=b");
}

void test_state_wording() {
    // The same sentences `ab-update status` prints, so the panel and the shell
    // never disagree about what happened.
    assert(describe_update_state("committed") == "committed");
    assert(describe_update_state("candidate-armed") == "candidate boot pending");
    assert(describe_update_state("fallback") == "candidate abandoned; committed slot retained");
    assert(describe_update_state("") == "no update has run on this device");
    // An unrecognized state is shown, not swallowed: a state the engine
    // publishes and this build has never heard of is exactly what an operator
    // needs to see.
    assert(describe_update_state("something-new") == "something-new");
}

void test_reads_the_files_ab_update_reads(const std::filesystem::path& work) {
    AboutPaths paths;
    paths.image_manifest = work / "image-manifest.env";
    paths.update_status = work / "run" / "status";
    paths.update_check = work / "run" / "check";
    paths.kernel_command_line = work / "cmdline";
    paths.hostname = work / "hostname";

    write_file(paths.image_manifest,
               "MICROPANEL_TOUCH_REVISION=5b3d9b3263ac2cdc2299d32bd7da319d4f5334f8\n"
               "LVGL_REVISION=85aa60d18b3d5e5588d7b247abf90198f07c8a63\n"
               "PANEL_PROFILE=luckfox-ctp-st7796s-gt911-portrait\n"
               "PANEL_VARIANT=luckfox-ctp\n"
               "IMAGE_LAYOUT=ab\n"
               "IMAGE_VERSION=00.39\n");
    write_file(paths.update_status, "state=committed\n");
    write_file(paths.update_check, "state=up-to-date\nversion=00.39\n");
    write_file(paths.kernel_command_line,
               "console=tty1 root=LABEL=MP_ROOT_A rootwait fsck.repair=yes\n");
    write_file(paths.hostname, "raspberrypi\n");

    const AboutInfo info = read_about_info(paths);
    assert(info.version == "00.39");
    assert(info.slot == "A");
    assert(info.app_revision == "5b3d9b3263ac2cdc2299d32bd7da319d4f5334f8");
    assert(info.panel_variant == "luckfox-ctp");
    assert(info.hostname == "raspberrypi");
    assert(info.update_state == "committed");
    assert(!info.update_candidate_pending);
    assert(info.last_check == "up-to-date (00.39)");

    // A 40-character revision on a 320 px panel is a wall of hex; the short
    // form is what identifies a build to a person.
    assert(row_value(info, "App") == "5b3d9b3");
    assert(row_value(info, "Version") == "00.39");
    assert(row_value(info, "Slot") == "A");
    assert(row_value(info, "Host") == "raspberrypi");

    write_file(paths.kernel_command_line, "console=tty1 root=LABEL=MP_ROOT_B rootwait\n");
    assert(read_about_info(paths).slot == "B");
}

void test_a_candidate_boot_is_flagged(const std::filesystem::path& work) {
    // The one boot where restarting undoes an update. Screens that offer a
    // restart need this, so it is a field rather than a sentence to re-parse.
    AboutPaths paths;
    paths.image_manifest = work / "image-manifest.env";
    paths.update_status = work / "run" / "status";
    paths.update_check = work / "run" / "check";
    paths.kernel_command_line = work / "cmdline";
    paths.hostname = work / "hostname";

    write_file(paths.update_status, "state=candidate-armed\n");
    const AboutInfo pending = read_about_info(paths);
    assert(pending.update_candidate_pending);
    assert(pending.update_state == "candidate boot pending");

    // Every other state, including the one that means a candidate was already
    // abandoned, must not raise it.
    for (const char* settled : {"committed", "fallback", "", "something-new"}) {
        write_file(paths.update_status, std::string("state=") + settled + "\n");
        assert(!read_about_info(paths).update_candidate_pending);
    }
}

void test_a_bare_device_says_so(const std::filesystem::path& work) {
    // Nothing published yet: a fresh flash that has never updated. Every field
    // must still read as a sentence rather than as an empty box.
    AboutPaths paths;
    paths.image_manifest = work / "absent" / "image-manifest.env";
    paths.update_status = work / "absent" / "status";
    paths.update_check = work / "absent" / "check";
    paths.kernel_command_line = work / "absent" / "cmdline";
    paths.hostname = work / "absent" / "hostname";

    const AboutInfo info = read_about_info(paths);
    assert(info.version == "unknown");
    assert(info.slot == "unknown");
    assert(info.update_state == "no update has run on this device");
    assert(info.last_check == "never run");
    // Optional rows are omitted rather than shown blank.
    assert(!has_row(info, "App"));
    assert(!has_row(info, "Panel"));
    assert(!has_row(info, "Host"));
    assert(has_row(info, "Version"));
    assert(has_row(info, "Update"));
}

}  // namespace

int main() {
    const std::filesystem::path work =
        std::filesystem::temp_directory_path() / "micropanel-touch-about-info-test";
    std::filesystem::remove_all(work);

    test_manifest_parsing();
    test_state_wording();
    test_reads_the_files_ab_update_reads(work);
    test_a_candidate_boot_is_flagged(work);
    test_a_bare_device_says_so(work);

    std::filesystem::remove_all(work);
    std::cout << "about-info: PASS\n";
    return 0;
}
