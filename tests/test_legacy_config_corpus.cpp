#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/LegacyConfig.h"

#include <cassert>
#include <string>

using micropanel_touch::core::LegacyConfig;

int main(int argc, char* argv[]) {
    // argv[0] plus the 14 entries pinned in fixtures/legacy/manifest.txt.
    assert(argc == 15);
    for (int index = 1; index < argc; ++index) {
        std::string diagnostic;
        const auto config = LegacyConfig::load(argv[index], &diagnostic);
        assert(config.has_value());
        assert(!config->modules().empty());
    }
    return 0;
}
