// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <vector>

namespace primeforge::discovery {

struct ProthSieveConfig {
    std::uint32_t k_start{};
    std::uint32_t k_stop{};
    std::uint32_t exponent{};
    std::uint64_t maximum_prime{};
    std::uint32_t thread_count{1U};
    std::uint64_t minimum_prime{3U};
};

struct ProthSieveResult {
    std::uint64_t candidate_count{};
    std::uint64_t eliminated_count{};
    std::uint64_t primes_applied{};
    std::vector<std::uint32_t> survivors;
};

[[nodiscard]] ProthSieveResult sieve_proth_candidates(const ProthSieveConfig& config);

}  // namespace primeforge::discovery
