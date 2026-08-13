// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/proth_sieve.hpp"

#include "primeforge/sieve/sieve.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace primeforge::discovery {
namespace {

[[nodiscard]] std::uint64_t modular_power(
    std::uint64_t base, std::uint64_t exponent, const std::uint64_t modulus) noexcept {
    std::uint64_t result = 1U;
    base %= modulus;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) result = (result * base) % modulus;
        exponent >>= 1U;
        if (exponent != 0U) base = (base * base) % modulus;
    }
    return result;
}

void validate(const ProthSieveConfig& config) {
    if (config.k_start < 3U || config.k_start > config.k_stop ||
        (config.k_start & 1U) == 0U || (config.k_stop & 1U) == 0U) {
        throw std::invalid_argument("Proth discovery k bounds must be ordered odd integers >= 3");
    }
    if (config.k_stop > 99'999'999U) {
        throw std::invalid_argument("Proth discovery k bound exceeds the pinned engine limit");
    }
    if (config.exponent < 32U || config.exponent > 99'999'999U) {
        throw std::invalid_argument("Proth discovery exponent exceeds the pinned engine domain");
    }
    if (config.maximum_prime < 3U || config.maximum_prime > 2'000'000'000U) {
        throw std::invalid_argument("Proth discovery sieve bound must be in [3, 2000000000]");
    }
}

}  // namespace

ProthSieveResult sieve_proth_candidates(const ProthSieveConfig& config) {
    validate(config);
    const auto candidate_count =
        (static_cast<std::uint64_t>(config.k_stop) - config.k_start) / 2U + 1U;
    if (candidate_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::length_error("Proth discovery candidate set does not fit in memory");
    }

    std::vector<bool> eliminated(static_cast<std::size_t>(candidate_count), false);
    const auto primes = sieve::generate_primes(3U, static_cast<std::uint64_t>(config.maximum_prime) + 1U);
    std::uint64_t eliminated_count = 0U;

    for (const auto prime : primes.primes) {
        if (prime == 2U) continue;
        const auto power = modular_power(2U, config.exponent, prime);
        const auto inverse = modular_power(power, prime - 2U, prime);
        const auto residue = (prime - inverse) % prime;
        const auto start_residue = static_cast<std::uint64_t>(config.k_start) % prime;
        const auto delta = (residue + prime - start_residue) % prime;
        std::uint64_t first = static_cast<std::uint64_t>(config.k_start) + delta;
        if ((first & 1U) == 0U) first += prime;

        const auto step = 2U * prime;
        for (auto k = first; k <= config.k_stop; k += step) {
            const auto index = static_cast<std::size_t>((k - config.k_start) / 2U);
            if (!eliminated[index]) {
                eliminated[index] = true;
                ++eliminated_count;
            }
        }
    }

    ProthSieveResult result;
    result.candidate_count = candidate_count;
    result.eliminated_count = eliminated_count;
    result.primes_applied = primes.primes.size();
    result.survivors.reserve(static_cast<std::size_t>(candidate_count - eliminated_count));
    for (std::size_t index = 0U; index < eliminated.size(); ++index) {
        if (!eliminated[index]) {
            result.survivors.push_back(
                config.k_start + static_cast<std::uint32_t>(2U * index));
        }
    }
    return result;
}

}  // namespace primeforge::discovery
