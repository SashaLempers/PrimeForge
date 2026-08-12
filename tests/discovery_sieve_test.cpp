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

}  // namespace

int main() {
    try {
        const primeforge::discovery::ProthSieveConfig config{3U, 43U, 32U, 97U};
        const auto result = primeforge::discovery::sieve_proth_candidates(config);
        const auto primes = primeforge::sieve::generate_primes(3U, 98U).primes;

        std::vector<std::uint32_t> expected;
        for (std::uint32_t k = config.k_start; k <= config.k_stop; k += 2U) {
            const auto candidate = (static_cast<std::uint64_t>(k) << config.exponent) + 1U;
            const bool eliminated = std::ranges::any_of(primes, [candidate](const auto prime) {
                return candidate % prime == 0U;
            });
            if (!eliminated) expected.push_back(k);
        }

        require(result.candidate_count == 21U, "candidate count mismatch");
        require(result.survivors == expected, "specialized sieve disagrees with direct division");
        require(result.eliminated_count + result.survivors.size() == result.candidate_count,
                "coverage mismatch");
        require(std::ranges::find(result.survivors, 43U) != result.survivors.end(),
                "known Proth prime was eliminated");

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
