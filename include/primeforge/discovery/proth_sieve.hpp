// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <vector>

namespace primeforge::discovery {

enum class WideInverseBackend { automatic, scalar };

struct ProthSieveConfig {
    std::uint32_t k_start{};
    std::uint32_t k_stop{};
    std::uint32_t exponent{};
    std::uint64_t maximum_prime{};
    std::uint32_t thread_count{1U};
    std::uint64_t minimum_prime{3U};
    WideInverseBackend wide_inverse_backend{WideInverseBackend::automatic};
};

struct ProthSieveResult {
    std::uint64_t candidate_count{};
    std::uint64_t eliminated_count{};
    std::uint64_t primes_applied{};
    std::uint64_t uint32_primes_processed{};
    std::uint64_t scalar_wide_primes_processed{};
    std::uint64_t avx512_ifma_primes_processed{};
    bool avx512_ifma_applied{};
    std::vector<std::uint32_t> survivors;
};

[[nodiscard]] ProthSieveResult sieve_proth_candidates(const ProthSieveConfig& config);

}  // namespace primeforge::discovery
