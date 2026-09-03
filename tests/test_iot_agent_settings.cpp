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

    const auto account = [](std::string user, std::string server) {
        IotAgentSettings settings;
        settings.user = std::move(user);
        settings.server = std::move(server);
        return settings;
    };
    assert(iot_agent_settings_are_valid(account("bot@example.org", "")));
    assert(iot_agent_settings_are_valid(account("bot@example.org", "xmpp.example.org")));
    assert(!iot_agent_settings_are_valid(account("", "")));
    assert(!iot_agent_settings_are_valid(account("bot", "")));
    assert(!iot_agent_settings_are_valid(account("bot@example.org", "not a host")));

    // Missing is not an error: a fresh panel simply has nothing to show.
    std::string diagnostic;
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());
    assert(diagnostic.empty());

    IotAgentSettings full = account("bot@example.org", "xmpp.example.org");
    full.port = 5223U;
    full.bosh = true;
    full.bosh_url = "https://xmpp.example.org:5281/http-bind";
    full.bosh_host = "example.org";
    full.admin = "owner@example.org";
    full.enabled = false;
    assert(save_iot_agent_settings(path, full, &diagnostic));
    struct stat metadata{};
    assert(stat(path.c_str(), &metadata) == 0);
    assert((metadata.st_mode & 0777) == 0640);
    auto loaded = load_iot_agent_settings(path, &diagnostic);
    assert(loaded.has_value());
    assert(loaded->same_account_as(full));
    assert(loaded->user == "bot@example.org");
    assert(loaded->server == "xmpp.example.org");
    assert(loaded->port == 5223U);
    assert(loaded->bosh);
    assert(loaded->bosh_url == "https://xmpp.example.org:5281/http-bind");
    assert(loaded->bosh_host == "example.org");
    assert(loaded->admin == "owner@example.org");
    assert(!loaded->enabled);

    // Empty optionals round-trip as empty, not as missing keys.
    assert(save_iot_agent_settings(path, account("other@example.net", ""), &diagnostic));
    loaded = load_iot_agent_settings(path, &diagnostic);
    assert(loaded.has_value());
    assert(loaded->user == "other@example.net");
    assert(loaded->server.empty());
    assert(loaded->port == 0U);
    assert(!loaded->bosh);
    assert(loaded->bosh_url.empty());
    assert(loaded->admin.empty());
    assert(loaded->enabled);
    assert(!loaded->same_account_as(full));

    // The password is never part of what is saved.
    {
        std::ifstream file(path);
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        assert(content.find("password") == std::string::npos);
        assert(content.find("pw") == std::string::npos);
    }

    // Invalid content is refused rather than half-applied.
    assert(!save_iot_agent_settings(path, account("bot", ""), &diagnostic));
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version=2\nuser=bot\nserver=-\nport=0\nbosh=0\nbosh_url=-\nbosh_host=-\nadmin=-\nenabled=1\n";
    }
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());
    assert(!diagnostic.empty());
    {
        // The version 1 file (account and server only) is carried over with
        // defaults for the rest.
        std::ofstream file(path, std::ios::trunc);
        file << "version=1\nuser=bot@example.org\nserver=xmpp.example.org\n";
    }
    loaded = load_iot_agent_settings(path, &diagnostic);
    assert(loaded.has_value());
    assert(loaded->user == "bot@example.org");
    assert(loaded->server == "xmpp.example.org");
    assert(loaded->port == 0U && !loaded->bosh && loaded->admin.empty() && loaded->enabled);
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version=1\nuser=bot@example.org\nserver=-\n";
    }
    loaded = load_iot_agent_settings(path, &diagnostic);
    assert(loaded.has_value() && loaded->server.empty());
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version=1\nuser=bot\nserver=-\n";
    }
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version=3\nuser=bot@example.org\nserver=-\n";
    }
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version=2\nuser=bot@example.org\nserver=-\nport=0\nbosh=1\nbosh_url=-\nbosh_host=-\nadmin=-\nenabled=1\n";
    }
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());  // BOSH without a URL
    {
        std::ofstream file(path, std::ios::trunc);
        file << "version=2\nuser=bot@example.org\nserver=-\nport=0\nbosh=0\nbosh_url=-\nbosh_host=-\nadmin=-\nenabled=1\npassword=oops\n";
    }
    assert(!load_iot_agent_settings(path, &diagnostic).has_value());

    fs::remove_all(root);
    return 0;
}
