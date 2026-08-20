#include "core/StaticIpSettings.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>

namespace micropanel_touch::core {

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

std::optional<unsigned int> parse_prefix_length(const std::string& value) {
    if (!is_valid_prefix_length(value)) {
        return std::nullopt;
    }
    unsigned int prefix = 0U;
    for (const unsigned char character : value) {
        prefix = prefix * 10U + static_cast<unsigned int>(character - '0');
    }
    return prefix;
}

namespace {

std::optional<std::array<unsigned int, 4>> parse_ipv4_octets(const std::string& value) {
    std::array<unsigned int, 4> octets{};
    std::size_t start = 0U;
    for (std::size_t index = 0U; index < octets.size(); ++index) {
        const std::size_t end = value.find('.', start);
        if ((index + 1U == octets.size()) != (end == std::string::npos)) {
            return std::nullopt;
        }
        const std::size_t length = (end == std::string::npos ? value.size() : end) - start;
        if (length == 0U || length > 3U) {
            return std::nullopt;
        }
        unsigned int octet = 0U;
        for (std::size_t character_index = start; character_index < start + length;
             ++character_index) {
            const unsigned char character = static_cast<unsigned char>(value[character_index]);
            if (std::isdigit(character) == 0) {
                return std::nullopt;
            }
            octet = octet * 10U + static_cast<unsigned int>(character - '0');
        }
        if (octet > 255U) {
            return std::nullopt;
        }
        octets[index] = octet;
        start = end == std::string::npos ? value.size() : end + 1U;
    }
    return octets;
}

std::optional<std::uint32_t> ipv4_value(const std::string& value) {
    const auto octets = parse_ipv4_octets(value);
    if (!octets.has_value()) {
        return std::nullopt;
    }
    return (static_cast<std::uint32_t>((*octets)[0]) << 24U) |
           (static_cast<std::uint32_t>((*octets)[1]) << 16U) |
           (static_cast<std::uint32_t>((*octets)[2]) << 8U) |
           static_cast<std::uint32_t>((*octets)[3]);
}

bool is_private_ipv4(const std::uint32_t value) {
    const unsigned int first = (value >> 24U) & 0xffU;
    const unsigned int second = (value >> 16U) & 0xffU;
    return first == 10U || (first == 172U && second >= 16U && second <= 31U) ||
           (first == 192U && second == 168U);
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

StaticIpValidationResult validate_dhcp_server_ipv4(const DhcpServerSettings& settings) {
    const auto address = ipv4_value(settings.address);
    const auto lease_start = ipv4_value(settings.lease_start);
    const auto lease_end = ipv4_value(settings.lease_end);
    if (!address.has_value()) {
        return {false, "Enter a valid DHCP server IPv4 address."};
    }
    if (!lease_start.has_value() || !lease_end.has_value()) {
        return {false, "Enter valid DHCP lease start and end addresses."};
    }
    const auto prefix = parse_prefix_length(settings.prefix_length);
    if (!prefix.has_value() || *prefix < 8U || *prefix > 30U) {
        return {false, "DHCP server prefix length must be from 8 to 30."};
    }
    if (!is_private_ipv4(*address)) {
        return {false, "DHCP server address must use a private IPv4 subnet."};
    }

    const std::uint32_t network_mask = 0xffffffffU << (32U - *prefix);
    const std::uint32_t host_mask = ~network_mask;
    const std::uint32_t network = *address & network_mask;
    const auto is_usable_host = [host_mask](const std::uint32_t value) {
        const std::uint32_t host = value & host_mask;
        return host != 0U && host != host_mask;
    };
    if (!is_usable_host(*address) || !is_usable_host(*lease_start) || !is_usable_host(*lease_end)) {
        return {false, "DHCP server and lease addresses must be usable hosts."};
    }
    if ((*lease_start & network_mask) != network || (*lease_end & network_mask) != network) {
        return {false, "DHCP lease range must be in the server subnet."};
    }
    if (*lease_start >= *lease_end) {
        return {false, "DHCP lease start must be before lease end."};
    }
    if (*address >= *lease_start && *address <= *lease_end) {
        return {false, "DHCP lease range must not include the server address."};
    }
    return {true, "DHCP server settings are valid; no network changes were made."};
}

std::optional<std::string> prefix_length_from_ipv4_netmask(const std::string& netmask) {
    const auto octets = parse_ipv4_octets(netmask);
    if (!octets.has_value()) {
        return std::nullopt;
    }
    bool encountered_zero = false;
    unsigned int prefix_length = 0U;
    for (const unsigned int octet : *octets) {
        for (int bit = 7; bit >= 0; --bit) {
            const bool set = (octet & (1U << static_cast<unsigned int>(bit))) != 0U;
            if (!set) {
                encountered_zero = true;
            } else if (encountered_zero) {
                return std::nullopt;
            } else {
                ++prefix_length;
            }
        }
    }
    return std::to_string(prefix_length);
}

}  // namespace micropanel_touch::core
