// SPDX-License-Identifier: Apache-2.0

#include "primeforge/proth/proth.hpp"

#include "primeforge/math/mul128.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace primeforge::proth {
namespace {

[[nodiscard]] std::uint64_t power_mod(
    std::uint64_t base, std::uint64_t exponent, const std::uint64_t modulus) {
    std::uint64_t result = 1U;
    base %= modulus;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = math::multiply_mod(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = math::multiply_mod(base, base, modulus);
        }
    }
    return result;
}

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string& text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] bool consume_prefix(
    std::string_view& input, const std::string_view prefix) noexcept {
    if (!input.starts_with(prefix)) return false;
    input.remove_prefix(prefix.size());
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> consume_decimal(
    std::string_view& input) noexcept {
    const auto end = input.find('"');
    if (end == std::string_view::npos) return std::nullopt;
    const auto decimal = input.substr(0U, end);
    if (decimal.empty() || (decimal.size() > 1U && decimal.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t value{};
    const auto parsed = std::from_chars(
        decimal.data(), decimal.data() + decimal.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != decimal.data() + decimal.size()) {
        return std::nullopt;
    }
    input.remove_prefix(end);
    return value;
}

}  // namespace

bool is_supported_u64(const std::uint64_t k, const std::uint32_t n) noexcept {
    if (k == 0U || (k & 1U) == 0U || n == 0U || n >= 64U) return false;
    if (k >= (std::uint64_t{1U} << n)) return false;
    return k <= (std::numeric_limits<std::uint64_t>::max() - 1U) >> n;
}

std::uint64_t candidate_value_u64(const std::uint64_t k, const std::uint32_t n) {
    if (k == 0U || (k & 1U) == 0U || n == 0U || n >= 64U ||
        k >= (std::uint64_t{1U} << n)) {
        throw std::invalid_argument("candidate is not a supported Proth form");
    }
    if (k > (std::numeric_limits<std::uint64_t>::max() - 1U) >> n) {
        throw std::overflow_error("Proth candidate exceeds uint64_t");
    }
    return (k << n) + 1U;
}

ProofAttempt try_prove_u64(
    const std::uint64_t k,
    const std::uint32_t n,
    const std::uint64_t maximum_witness) {
    if (maximum_witness < 2U) {
        throw std::invalid_argument("maximum Proth witness must be at least two");
    }
    const auto value = candidate_value_u64(k, n);
    const auto final_witness = std::min(maximum_witness, value - 1U);
    ProofAttempt result;
    for (std::uint64_t witness = 2U;; ++witness) {
        ++result.bases_tested;
        const auto residue = power_mod(witness, (value - 1U) / 2U, value);
        if (residue == value - 1U) {
            result.primality = PrimalityStatus::proven_prime;
            result.certificate = Certificate{
                certificate_format, k, n, value, witness, residue};
            return result;
        }
        if (witness == final_witness) break;
    }
    return result;
}

bool verify_u64(const Certificate& certificate) noexcept {
    try {
        if (certificate.format_version != certificate_format || certificate.witness < 2U) {
            return false;
        }
        const auto value = candidate_value_u64(certificate.k, certificate.n);
        if (certificate.value != value || certificate.witness >= value ||
            certificate.residue != value - 1U) {
            return false;
        }
        return power_mod(certificate.witness, (value - 1U) / 2U, value) ==
               value - 1U;
    } catch (...) {
        return false;
    }
}

std::string canonical_certificate(const Certificate& certificate) {
    if (!verify_u64(certificate)) {
        throw std::invalid_argument("invalid Proth certificate");
    }
    return "{\"format_version\":\"" + certificate.format_version +
           "\",\"k\":\"" + std::to_string(certificate.k) +
           "\",\"n\":\"" + std::to_string(certificate.n) +
           "\",\"residue\":\"" + std::to_string(certificate.residue) +
           "\",\"value\":\"" + std::to_string(certificate.value) +
           "\",\"witness\":\"" + std::to_string(certificate.witness) + "\"}";
}

std::optional<Certificate> parse_canonical_certificate(
    const std::string_view bytes) noexcept {
    try {
        std::string_view remaining = bytes;
        constexpr std::string_view prefix =
            "{\"format_version\":\"primeforge.proth.certificate.u64.v1\",\"k\":\"";
        if (!consume_prefix(remaining, prefix)) return std::nullopt;
        const auto k = consume_decimal(remaining);
        if (!k.has_value() || !consume_prefix(remaining, "\",\"n\":\"")) {
            return std::nullopt;
        }
        const auto n = consume_decimal(remaining);
        if (!n.has_value() || *n > std::numeric_limits<std::uint32_t>::max() ||
            !consume_prefix(remaining, "\",\"residue\":\"")) {
            return std::nullopt;
        }
        const auto residue = consume_decimal(remaining);
        if (!residue.has_value() ||
            !consume_prefix(remaining, "\",\"value\":\"")) {
            return std::nullopt;
        }
        const auto value = consume_decimal(remaining);
        if (!value.has_value() ||
            !consume_prefix(remaining, "\",\"witness\":\"")) {
            return std::nullopt;
        }
        const auto witness = consume_decimal(remaining);
        if (!witness.has_value() || !consume_prefix(remaining, "\"}") ||
            !remaining.empty()) {
            return std::nullopt;
        }
        Certificate certificate{
            certificate_format, *k, static_cast<std::uint32_t>(*n),
            *value, *witness, *residue};
        if (!verify_u64(certificate) || canonical_certificate(certificate) != bytes) {
            return std::nullopt;
        }
        return certificate;
    } catch (...) {
        return std::nullopt;
    }
}

std::string certificate_sha256(
    const Certificate& certificate, const Sha256Provider& sha256) {
    const auto canonical = canonical_certificate(certificate);
    return sha256_to_hex(sha256.digest(bytes_of(canonical)));
}

}  // namespace primeforge::proth
