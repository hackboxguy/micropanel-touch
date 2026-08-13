#ifdef NDEBUG
#undef NDEBUG
#endif

#include "platform/NetworkInfo.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
    const std::vector<std::string> names =
        micropanel_touch::platform::parse_nmcli_connection_names(
            "Wired\\: profile\nLiteral\\\\backslash\n\n");
    assert(names.size() == 2U);
    assert(names[0] == "Wired: profile");
    assert(names[1] == "Literal\\backslash");
    return 0;
}
