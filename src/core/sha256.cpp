#include "primeforge/core/sha256.hpp"

#include <algorithm>

namespace primeforge {
namespace {

[[nodiscard]] constexpr int hex_value(const char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

} // namespace

std::string sha256_to_hex(const Sha256Digest& digest) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.resize(digest.size() * 2U);

    for (std::size_t index = 0; index < digest.size(); ++index) {
        const auto value = std::to_integer<unsigned int>(digest[index]);
        result[index * 2U] = digits[(value >> 4U) & 0x0fU];
        result[index * 2U + 1U] = digits[value & 0x0fU];
    }
    return result;
}

std::optional<Sha256Digest> sha256_from_hex(const std::string_view hex) noexcept {
    Sha256Digest result{};
    if (hex.size() != result.size() * 2U) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < result.size(); ++index) {
        const int high = hex_value(hex[index * 2U]);
        const int low = hex_value(hex[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        const auto value = static_cast<unsigned int>((high << 4) | low);
        result[index] = static_cast<std::byte>(value);
    }
    return result;
}

} // namespace primeforge
