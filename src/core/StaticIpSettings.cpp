#include "core/StaticIpSettings.h"

#include <cctype>
#include <limits>

namespace micropanel_touch::core {
namespace {

bool is_valid_ipv4(const std::string& value) {
    if (value.empty() || value.back() == '.') {
        return false;
    }

    int octets = 0;
    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t end = value.find('.', start);
        const std::size_t length = (end == std::string::npos ? value.size() : end) - start;
        if (length == 0U || length > 3U) {
            return false;
        }

        unsigned int octet = 0;
        for (std::size_t index = start; index < start + length; ++index) {
            const unsigned char character = static_cast<unsigned char>(value[index]);
            if (std::isdigit(character) == 0) {
                return false;
            }
            octet = octet * 10U + static_cast<unsigned int>(character - '0');
        }
        if (octet > 255U) {
            return false;
        }

        ++octets;
        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
    return octets == 4;
}

bool is_valid_prefix_length(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    unsigned int prefix = 0;
    for (const unsigned char character : value) {
        if (std::isdigit(character) == 0) {
            return false;
        }
        if (prefix > (std::numeric_limits<unsigned int>::max() - 9U) / 10U) {
            return false;
        }
        prefix = prefix * 10U + static_cast<unsigned int>(character - '0');
    }
    return prefix <= 32U;
}

}  // namespace

StaticIpValidationResult validate_static_ipv4(const StaticIpSettings& settings) {
    if (!is_valid_ipv4(settings.address)) {
        return {false, "Enter a valid IPv4 address."};
    }
    if (!is_valid_prefix_length(settings.prefix_length)) {
        return {false, "Prefix length must be from 0 to 32."};
    }
    if (!is_valid_ipv4(settings.gateway)) {
        return {false, "Enter a valid IPv4 gateway."};
    }
    return {true, "Inputs are valid; no network changes were made."};
}

}  // namespace micropanel_touch::core
