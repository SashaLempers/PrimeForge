// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/proth_sieve.hpp"
#include "primeforge/math/mul128.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>
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

[[nodiscard]] std::uint64_t reference_power_wide(
    std::uint64_t base, std::uint32_t exponent, const std::uint64_t modulus) {
    std::uint64_t result = 1U;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = primeforge::math::multiply_mod_portable_reference(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = primeforge::math::multiply_mod_portable_reference(base, base, modulus);
        }
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

        auto parallel_config = differential_cases.back();
        parallel_config.thread_count = 4U;
        const auto serial_result =
            primeforge::discovery::sieve_proth_candidates(differential_cases.back());
        const auto parallel_result =
            primeforge::discovery::sieve_proth_candidates(parallel_config);
        require(parallel_result.survivors == serial_result.survivors,
                "parallel prime-range partition changed the survivor set");
        require(parallel_result.primes_applied == serial_result.primes_applied,
                "parallel prime-range partition has a gap or duplicate");

        auto lower_interval = differential_cases.back();
        lower_interval.maximum_prime = 47U;
        auto upper_interval = differential_cases.back();
        upper_interval.minimum_prime = 48U;
        const auto lower_result =
            primeforge::discovery::sieve_proth_candidates(lower_interval);
        const auto upper_result =
            primeforge::discovery::sieve_proth_candidates(upper_interval);
        std::vector<std::uint32_t> split_survivors;
        std::ranges::set_intersection(
            lower_result.survivors, upper_result.survivors,
            std::back_inserter(split_survivors));
        require(split_survivors == serial_result.survivors,
                "adjacent sieve intervals have a gap or overlap error");
        require(lower_result.primes_applied + upper_result.primes_applied ==
                    serial_result.primes_applied,
                "adjacent sieve intervals changed the applied-prime count");

        constexpr std::uint32_t wide_exponent = 32U;
        std::uint64_t wide_prime = std::numeric_limits<std::uint32_t>::max() + 2ULL;
        std::uint32_t wide_k = 0U;
        for (; wide_k == 0U; wide_prime += 2U) {
            if (!primeforge::sieve::is_prime_u64(wide_prime)) continue;
            const auto inverse = reference_power_wide(
                (wide_prime + 1U) / 2U, wide_exponent, wide_prime);
            const auto residue = (wide_prime - inverse) % wide_prime;
            if (residue >= 3U && residue <= 99'999'999U && (residue & 1U) != 0U) {
                wide_k = static_cast<std::uint32_t>(residue);
            }
        }
        wide_prime -= 2U;
        const primeforge::discovery::ProthSieveConfig wide_config{
            wide_k, wide_k, wide_exponent, wide_prime, 1U, wide_prime};
        const auto wide_result = primeforge::discovery::sieve_proth_candidates(wide_config);
        require(wide_result.primes_applied == 1U,
                "isolated wide-prime interval has a gap or duplicate");
        require(wide_result.survivors.empty(),
                "wide modular inverse failed to eliminate a known divisible candidate");

        std::vector<std::pair<std::uint64_t, std::uint32_t>> wide_batch_factors;
        for (auto candidate_prime = std::numeric_limits<std::uint32_t>::max() + 2ULL;
             wide_batch_factors.size() < 7U; candidate_prime += 2U) {
            if (!primeforge::sieve::is_prime_u64(candidate_prime)) continue;
            const auto inverse = reference_power_wide(
                (candidate_prime + 1U) / 2U, wide_exponent, candidate_prime);
            const auto residue = (candidate_prime - inverse) % candidate_prime;
            if (residue >= 3U && residue <= 99'999'999U && (residue & 1U) != 0U) {
                wide_batch_factors.emplace_back(
                    candidate_prime, static_cast<std::uint32_t>(residue));
            }
        }
        const auto wide_batch_start = wide_batch_factors.front().first;
        auto wide_batch_stop = wide_batch_factors.back().first;
        std::uint64_t wide_batch_prime_count = 0U;
        for (auto candidate_prime = wide_batch_start;
             candidate_prime <= wide_batch_stop; candidate_prime += 2U) {
            if (primeforge::sieve::is_prime_u64(candidate_prime)) ++wide_batch_prime_count;
        }
        while ((wide_batch_prime_count % 4U) == 0U) {
            do {
                wide_batch_stop += 2U;
            } while (!primeforge::sieve::is_prime_u64(wide_batch_stop));
            ++wide_batch_prime_count;
        }
        for (const auto& [factor, k] : wide_batch_factors) {
            static_cast<void>(factor);
            const primeforge::discovery::ProthSieveConfig batch_config{
                k, k, wide_exponent, wide_batch_stop, 1U, wide_batch_start};
            const auto batch_result =
                primeforge::discovery::sieve_proth_candidates(batch_config);
            require(batch_result.primes_applied == wide_batch_prime_count,
                    "wide inverse batch has a prime-count gap or duplicate");
            require(batch_result.survivors.empty(),
                    "wide inverse batch failed to eliminate a known divisible candidate");
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
                {3U, 43U, 32U, 9'223'372'036'854'775'808ULL}));
        } catch (const std::invalid_argument&) {
            rejected_excessive_sieve_bound = true;
        }
        require(rejected_excessive_sieve_bound,
                "sieve bound above the validated implementation limit was accepted");

        bool rejected_excessive_threads = false;
        try {
            static_cast<void>(primeforge::discovery::sieve_proth_candidates(
                {3U, 43U, 32U, 97U, 65U}));
        } catch (const std::invalid_argument&) {
            rejected_excessive_threads = true;
        }
        require(rejected_excessive_threads, "excessive sieve thread count was accepted");

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
