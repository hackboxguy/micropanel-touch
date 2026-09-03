#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/IotAgentSettings.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

int main() {
    using micropanel_touch::platform::IotAgentSettings;
    using micropanel_touch::platform::iot_agent_settings_are_valid;
    using micropanel_touch::platform::load_iot_agent_settings;
    using micropanel_touch::platform::save_iot_agent_settings;

    const fs::path root =
        fs::temp_directory_path() / ("micropanel-touch-iot-agent-" + std::to_string(getpid()));
    fs::create_directories(root);
    const fs::path path = root / "iot-agent.conf";

    assert(iot_agent_settings_are_valid({"bot@example.org", ""}));
    assert(iot_agent_settings_are_valid({"bot@example.org", "xmpp.example.org"}));
    assert(!iot_agent_settings_are_valid({"", ""}));
    assert(!iot_agent_settings_are_valid({"bot", ""}));
    assert(!iot_agent_settings_are_valid({"bot@example.org", "not a host"}));

    // Missing is not an error: a fresh panel simply has nothing to show.
    std::string diagnostic;
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());
    assert(diagnostic.empty());

    assert(save_iot_agent_settings(path, {"bot@example.org", "xmpp.example.org"}, &diagnostic));
    struct stat metadata{};
    assert(stat(path.c_str(), &metadata) == 0);
    assert((metadata.st_mode & 0777) == 0640);
    auto loaded = load_iot_agent_settings(path, &diagnostic);
    assert(loaded.has_value());
    assert(loaded->user == "bot@example.org");
    assert(loaded->server == "xmpp.example.org");

    // An empty server round-trips as empty, not as a missing key.
    assert(save_iot_agent_settings(path, {"other@example.net", ""}, &diagnostic));
    loaded = load_iot_agent_settings(path, &diagnostic);
    assert(loaded.has_value());
    assert(loaded->user == "other@example.net");
    assert(loaded->server.empty());

    // The password is never part of what is saved.
    {
        std::ifstream file(path);
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        assert(content.find("password") == std::string::npos);
        assert(content.find("pw") == std::string::npos);
    }

    // Invalid content is refused rather than half-applied.
    assert(!save_iot_agent_settings(path, {"bot", ""}, &diagnostic));
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version=1\nuser=bot\nserver=\n";
    }
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());
    assert(!diagnostic.empty());
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version=2\nuser=bot@example.org\nserver=\n";
    }
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version=1\nuser=bot@example.org\nserver=\npassword=oops\n";
    }
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());

    fs::remove_all(root);
    return 0;
}
