// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"
#include "primeforge/core/status.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace primeforge::proth {

inline constexpr char certificate_format[] = "primeforge.proth.certificate.u64.v1";

struct Certificate {
    std::string format_version{certificate_format};
    std::uint64_t k{};
    std::uint32_t n{};
    std::uint64_t value{};
    std::uint64_t witness{};
    std::uint64_t residue{};

    [[nodiscard]] bool operator==(const Certificate&) const = default;
};

struct ProofAttempt {
    PrimalityStatus primality{PrimalityStatus::untested};
    std::uint64_t bases_tested{};
    std::uint64_t modular_exponentiations{};
    std::optional<Certificate> certificate;
};

// This bounded path accepts only Proth numbers whose complete value fits u64.
[[nodiscard]] bool is_supported_u64(std::uint64_t k, std::uint32_t n) noexcept;
[[nodiscard]] std::uint64_t candidate_value_u64(std::uint64_t k, std::uint32_t n);

// Failure to find a witness leaves the result UNTESTED; it never proves composition.
[[nodiscard]] ProofAttempt try_prove_u64(
    std::uint64_t k, std::uint32_t n, std::uint64_t maximum_witness);
[[nodiscard]] bool verify_u64(const Certificate& certificate) noexcept;
[[nodiscard]] std::string canonical_certificate(const Certificate& certificate);
// Accepts only the exact canonical v1 byte representation. Malformed, noncanonical,
// or mathematically invalid certificates are rejected without throwing.
[[nodiscard]] std::optional<Certificate> parse_canonical_certificate(
    std::string_view bytes) noexcept;
[[nodiscard]] std::string certificate_sha256(
    const Certificate& certificate, const Sha256Provider& sha256);

}  // namespace primeforge::proth
