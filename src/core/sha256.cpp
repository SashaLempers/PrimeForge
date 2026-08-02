// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

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

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

[[nodiscard]] constexpr std::uint32_t load_big_endian(const std::byte* bytes) noexcept {
    return (std::to_integer<std::uint32_t>(bytes[0]) << 24U) |
           (std::to_integer<std::uint32_t>(bytes[1]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[2]) << 8U) |
           std::to_integer<std::uint32_t>(bytes[3]);
}

void store_big_endian(const std::uint32_t value, std::byte* output) noexcept {
    output[0] = static_cast<std::byte>(value >> 24U);
    output[1] = static_cast<std::byte>(value >> 16U);
    output[2] = static_cast<std::byte>(value >> 8U);
    output[3] = static_cast<std::byte>(value);
}

} // namespace

Sha256Digest sha256(const std::span<const std::byte> bytes) {
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8U) {
        throw std::length_error("SHA-256 input is too long");
    }
    std::vector<std::byte> padded(bytes.begin(), bytes.end());
    padded.push_back(std::byte{0x80U});
    while (padded.size() % 64U != 56U) {
        padded.push_back(std::byte{0U});
    }
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<std::byte>(bit_length >> static_cast<unsigned>(shift)));
    }

    std::array<std::uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t offset = 0U; offset < padded.size(); offset += 64U) {
        for (std::size_t index = 0U; index < 16U; ++index) {
            schedule[index] = load_big_endian(padded.data() + offset + index * 4U);
        }
        for (std::size_t index = 16U; index < schedule.size(); ++index) {
            const auto sigma0 = std::rotr(schedule[index - 15U], 7) ^
                                std::rotr(schedule[index - 15U], 18) ^
                                (schedule[index - 15U] >> 3U);
            const auto sigma1 = std::rotr(schedule[index - 2U], 17) ^
                                std::rotr(schedule[index - 2U], 19) ^
                                (schedule[index - 2U] >> 10U);
            schedule[index] = schedule[index - 16U] + sigma0 + schedule[index - 7U] + sigma1;
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (std::size_t index = 0U; index < schedule.size(); ++index) {
            const auto choice = (e & f) ^ (~e & g);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto temporary1 = h + sum1 + choice + round_constants[index] + schedule[index];
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    Sha256Digest result{};
    for (std::size_t index = 0U; index < state.size(); ++index) {
        store_big_endian(state[index], result.data() + index * 4U);
    }
    return result;
}

Sha256Digest PortableSha256Provider::digest(const std::span<const std::byte> bytes) const {
    return sha256(bytes);
}

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
