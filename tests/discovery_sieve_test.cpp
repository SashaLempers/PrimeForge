// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/proth_sieve.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] std::uint64_t reference_power(
    std::uint64_t base, std::uint32_t exponent, const std::uint64_t modulus) {
    std::uint64_t result = 1U;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) result = (result * base) % modulus;
        exponent >>= 1U;
        if (exponent != 0U) base = (base * base) % modulus;
    }
    return result;
}

[[nodiscard]] std::vector<std::uint32_t> reference_survivors(
    const primeforge::discovery::ProthSieveConfig& config) {
    const auto primes = primeforge::sieve::generate_primes(
        3U, static_cast<std::uint64_t>(config.maximum_prime) + 1U).primes;
    std::vector<std::uint32_t> expected;
    for (auto k = config.k_start; k <= config.k_stop; k += 2U) {
        const bool eliminated = std::ranges::any_of(primes, [&](const auto prime) {
            const auto power = reference_power(2U, config.exponent, prime);
            return (static_cast<std::uint64_t>(k) * power + 1U) % prime == 0U;
        });
        if (!eliminated) expected.push_back(k);
    }
    return expected;
}

}  // namespace

int main() {
    try {
        const primeforge::discovery::ProthSieveConfig config{3U, 43U, 32U, 97U};
        const auto result = primeforge::discovery::sieve_proth_candidates(config);
        const auto expected = reference_survivors(config);

        require(result.candidate_count == 21U, "candidate count mismatch");
        require(result.survivors == expected, "specialized sieve disagrees with direct division");
        require(result.eliminated_count + result.survivors.size() == result.candidate_count,
                "coverage mismatch");
        require(std::ranges::find(result.survivors, 43U) != result.survivors.end(),
                "known Proth prime was eliminated");

        const std::vector<primeforge::discovery::ProthSieveConfig> differential_cases{
            {101U, 1'001U, 97U, 997U},
            {10'001U, 12'001U, 33'326U, 10'007U},
            {75'939'069U, 75'940'069U, 66'411U, 65'521U},
        };
        for (const auto& differential : differential_cases) {
            const auto actual =
                primeforge::discovery::sieve_proth_candidates(differential).survivors;
            require(actual == reference_survivors(differential),
                    "direct inverse/segmented sieve disagrees with independent modular division");
        }

        bool rejected_even_bound = false;
        try {
            static_cast<void>(primeforge::discovery::sieve_proth_candidates({4U, 43U, 32U, 97U}));
        } catch (const std::invalid_argument&) {
            rejected_even_bound = true;
        }
        require(rejected_even_bound, "even k bound was accepted");

        bool rejected_out_of_engine_domain = false;
        try {
            static_cast<void>(primeforge::discovery::sieve_proth_candidates(
                {1'227'250'535U, 1'227'330'535U, 33'326U, 65'521U}));
        } catch (const std::invalid_argument&) {
            rejected_out_of_engine_domain = true;
        }
        require(rejected_out_of_engine_domain,
                "k range outside the pinned proof-engine domain was accepted");

        bool rejected_excessive_sieve_bound = false;
        try {
            static_cast<void>(primeforge::discovery::sieve_proth_candidates(
                {3U, 43U, 32U, 4'000'000'001U}));
        } catch (const std::invalid_argument&) {
            rejected_excessive_sieve_bound = true;
        }
        require(rejected_excessive_sieve_bound,
                "sieve bound above the validated implementation limit was accepted");

        std::cout << "discovery_sieve.candidates=" << result.candidate_count << '\n'
                  << "discovery_sieve.eliminated=" << result.eliminated_count << '\n'
                  << "discovery_sieve.survivors=" << result.survivors.size() << '\n'
                  << "discovery_sieve.status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "discovery_sieve_test: " << error.what() << '\n';
        return 1;
    }
}
