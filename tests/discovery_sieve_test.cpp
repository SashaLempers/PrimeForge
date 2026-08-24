// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/proth_sieve.hpp"
#include "primeforge/core/system_info.hpp"
#include "primeforge/discovery/detail/wide_montgomery_avx512.hpp"
#include "primeforge/math/mul128.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <algorithm>
#include <array>
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
        const bool hardware_avx512_ifma =
            primeforge::collect_cpu_capabilities().avx512ifma;

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
        while (wide_batch_prime_count < 15U ||
               (wide_batch_prime_count % 4U) == 0U) {
            do {
                wide_batch_stop += 2U;
            } while (!primeforge::sieve::is_prime_u64(wide_batch_stop));
            ++wide_batch_prime_count;
        }
        require(wide_batch_prime_count >= 8U,
                "wide inverse dispatch test did not contain a full IFMA batch");
        bool observed_avx512_ifma = false;
        for (const auto& [factor, k] : wide_batch_factors) {
            static_cast<void>(factor);
            primeforge::discovery::ProthSieveConfig batch_config{
                k, k, wide_exponent, wide_batch_stop, 1U, wide_batch_start};
            const auto batch_result =
                primeforge::discovery::sieve_proth_candidates(batch_config);
            observed_avx512_ifma =
                observed_avx512_ifma || batch_result.avx512_ifma_applied;
            require(batch_result.primes_applied == wide_batch_prime_count,
                    "wide inverse batch has a prime-count gap or duplicate");
            require(batch_result.survivors.empty(),
                    "wide inverse batch failed to eliminate a known divisible candidate");
            batch_config.wide_inverse_backend =
                primeforge::discovery::WideInverseBackend::scalar;
            const auto scalar_batch_result =
                primeforge::discovery::sieve_proth_candidates(batch_config);
            require(scalar_batch_result.survivors == batch_result.survivors &&
                        scalar_batch_result.eliminated_count == batch_result.eliminated_count &&
                        scalar_batch_result.primes_applied == batch_result.primes_applied,
                    "IFMA and scalar batches disagree on a known divisible candidate");
        }

        constexpr std::array<std::uint32_t, 8U> control_candidates{
            3U, 5U, 7U, 11U, 101U, 10'001U, 1'000'001U, 99'999'999U};
        for (const auto k : control_candidates) {
            primeforge::discovery::ProthSieveConfig automatic{
                k, k, wide_exponent, wide_batch_stop, 1U, wide_batch_start};
            auto scalar = automatic;
            scalar.wide_inverse_backend =
                primeforge::discovery::WideInverseBackend::scalar;
            const auto automatic_result =
                primeforge::discovery::sieve_proth_candidates(automatic);
            const auto scalar_result =
                primeforge::discovery::sieve_proth_candidates(scalar);
            require(automatic_result.survivors == scalar_result.survivors &&
                        automatic_result.eliminated_count == scalar_result.eliminated_count &&
                        automatic_result.primes_applied == scalar_result.primes_applied,
                    "IFMA and scalar batches disagree on a control candidate");
        }

        primeforge::discovery::ProthSieveConfig automatic_parallel{
            3U, 10'001U, wide_exponent, wide_batch_stop, 4U, wide_batch_start};
        auto scalar_parallel = automatic_parallel;
        scalar_parallel.wide_inverse_backend =
            primeforge::discovery::WideInverseBackend::scalar;
        auto automatic_serial = automatic_parallel;
        automatic_serial.thread_count = 1U;
        const auto automatic_parallel_result =
            primeforge::discovery::sieve_proth_candidates(automatic_parallel);
        const auto scalar_parallel_result =
            primeforge::discovery::sieve_proth_candidates(scalar_parallel);
        const auto automatic_serial_result =
            primeforge::discovery::sieve_proth_candidates(automatic_serial);
        require(automatic_parallel_result.survivors == scalar_parallel_result.survivors &&
                    automatic_parallel_result.survivors == automatic_serial_result.survivors &&
                    automatic_parallel_result.eliminated_count ==
                        scalar_parallel_result.eliminated_count &&
                    automatic_parallel_result.primes_applied ==
                        scalar_parallel_result.primes_applied &&
                    automatic_parallel_result.primes_applied ==
                        automatic_serial_result.primes_applied,
                "IFMA worker partition changed the exact sieve result");

        std::array<std::uint64_t,
                   primeforge::discovery::detail::avx512_ifma_lane_count> tail_stops{};
        std::array<std::uint32_t,
                   primeforge::discovery::detail::avx512_ifma_lane_count> tail_witnesses{};
        std::vector<std::uint32_t> preceding_factor_residues;
        std::uint64_t tail_prime_count = 0U;
        for (auto candidate_prime = wide_batch_start;
             std::ranges::any_of(tail_stops, [](const auto value) { return value == 0U; });
             candidate_prime += 2U) {
            if (!primeforge::sieve::is_prime_u64(candidate_prime)) continue;
            ++tail_prime_count;
            const auto inverse = reference_power_wide(
                (candidate_prime + 1U) / 2U, wide_exponent, candidate_prime);
            const auto residue = (candidate_prime - inverse) % candidate_prime;
            const auto admissible_residue =
                residue >= 3U && residue <= 99'999'999U && (residue & 1U) != 0U
                ? static_cast<std::uint32_t>(residue)
                : 0U;
            if (tail_prime_count <
                primeforge::discovery::detail::avx512_ifma_lane_count) {
                preceding_factor_residues.push_back(admissible_residue);
                continue;
            }
            const auto remainder = static_cast<std::size_t>(
                tail_prime_count %
                primeforge::discovery::detail::avx512_ifma_lane_count);
            if (tail_stops[remainder] == 0U && remainder == 0U) {
                tail_stops[remainder] = candidate_prime;
                tail_witnesses[remainder] = 3U;
            } else if (tail_stops[remainder] == 0U && admissible_residue != 0U &&
                       std::ranges::find(preceding_factor_residues, admissible_residue) ==
                           preceding_factor_residues.end()) {
                tail_stops[remainder] = candidate_prime;
                tail_witnesses[remainder] = admissible_residue;
            }
            preceding_factor_residues.push_back(admissible_residue);
        }
        for (std::size_t remainder = 0U; remainder < tail_stops.size(); ++remainder) {
            primeforge::discovery::ProthSieveConfig automatic{
                tail_witnesses[remainder], tail_witnesses[remainder], wide_exponent,
                tail_stops[remainder], 1U, wide_batch_start};
            auto scalar = automatic;
            scalar.wide_inverse_backend =
                primeforge::discovery::WideInverseBackend::scalar;
            const auto automatic_result =
                primeforge::discovery::sieve_proth_candidates(automatic);
            const auto scalar_result =
                primeforge::discovery::sieve_proth_candidates(scalar);
            require(automatic_result.survivors == scalar_result.survivors &&
                        automatic_result.eliminated_count == scalar_result.eliminated_count &&
                        automatic_result.primes_applied == scalar_result.primes_applied &&
                        automatic_result.primes_applied %
                            primeforge::discovery::detail::avx512_ifma_lane_count == remainder,
                    "IFMA tail remainder disagrees with the scalar backend");
            if (hardware_avx512_ifma) {
                require(automatic_result.avx512_ifma_primes_processed ==
                            automatic_result.primes_applied - remainder &&
                            automatic_result.scalar_wide_primes_processed == remainder,
                        "IFMA tail processing counters do not cover the exact remainder");
            } else {
                require(automatic_result.avx512_ifma_primes_processed == 0U &&
                            automatic_result.scalar_wide_primes_processed ==
                                automatic_result.primes_applied,
                        "non-IFMA host did not process every wide prime through scalar fallback");
            }
            if (remainder != 0U) {
                require(automatic_result.survivors.empty() && scalar_result.survivors.empty(),
                        "IFMA scalar tail failed to apply its terminal factor witness");
            }
        }

        primeforge::discovery::ProthSieveConfig transition_52{
            3U, 10'001U, wide_exponent,
            primeforge::discovery::detail::avx512_ifma_modulus_limit + 1'000U,
            1U,
            primeforge::discovery::detail::avx512_ifma_modulus_limit - 1'000U};
        auto scalar_transition_52 = transition_52;
        scalar_transition_52.wide_inverse_backend =
            primeforge::discovery::WideInverseBackend::scalar;
        const auto automatic_transition_52_result =
            primeforge::discovery::sieve_proth_candidates(transition_52);
        const auto scalar_transition_52_result =
            primeforge::discovery::sieve_proth_candidates(scalar_transition_52);
        require(automatic_transition_52_result.survivors ==
                    scalar_transition_52_result.survivors &&
                    automatic_transition_52_result.eliminated_count ==
                        scalar_transition_52_result.eliminated_count &&
                    automatic_transition_52_result.primes_applied ==
                        scalar_transition_52_result.primes_applied,
                "IFMA-to-scalar 2^52 transition changed the exact sieve result");
        require(scalar_transition_52_result.avx512_ifma_primes_processed == 0U &&
                    scalar_transition_52_result.scalar_wide_primes_processed ==
                        scalar_transition_52_result.primes_applied,
                "explicit scalar backend did not cover the 2^52 transition");
        if (hardware_avx512_ifma) {
            require(automatic_transition_52_result.avx512_ifma_applied &&
                        automatic_transition_52_result.avx512_ifma_primes_processed > 0U &&
                        automatic_transition_52_result.scalar_wide_primes_processed > 0U,
                    "automatic backend did not exercise both sides of the 2^52 transition");
        }

        const auto transition_32_center =
            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
        primeforge::discovery::ProthSieveConfig transition_32{
            3U, 10'001U, wide_exponent, transition_32_center + 1'000U,
            1U, transition_32_center - 1'000U};
        auto scalar_transition_32 = transition_32;
        scalar_transition_32.wide_inverse_backend =
            primeforge::discovery::WideInverseBackend::scalar;
        const auto automatic_transition_32_result =
            primeforge::discovery::sieve_proth_candidates(transition_32);
        const auto scalar_transition_32_result =
            primeforge::discovery::sieve_proth_candidates(scalar_transition_32);
        require(automatic_transition_32_result.survivors ==
                    scalar_transition_32_result.survivors &&
                    automatic_transition_32_result.eliminated_count ==
                        scalar_transition_32_result.eliminated_count &&
                    automatic_transition_32_result.primes_applied ==
                        scalar_transition_32_result.primes_applied,
                "32-bit-to-IFMA transition changed the exact sieve result");
        require(automatic_transition_32_result.uint32_primes_processed > 0U,
                "automatic backend did not exercise the 32-bit side of its transition");
        if (hardware_avx512_ifma) {
            require(automatic_transition_32_result.avx512_ifma_applied &&
                        automatic_transition_32_result.avx512_ifma_primes_processed > 0U,
                    "automatic backend did not exercise the IFMA side of the 32-bit transition");
        } else {
            require(automatic_transition_32_result.scalar_wide_primes_processed > 0U,
                    "non-IFMA host did not exercise scalar fallback above the 32-bit transition");
        }
        require(observed_avx512_ifma ==
                    hardware_avx512_ifma,
                "wide sieve AVX-512IFMA runtime dispatch disagrees with CPU capability");

        auto scalar_wide_config = wide_config;
        scalar_wide_config.wide_inverse_backend =
            primeforge::discovery::WideInverseBackend::scalar;
        const auto scalar_wide_result =
            primeforge::discovery::sieve_proth_candidates(scalar_wide_config);
        require(!scalar_wide_result.avx512_ifma_applied,
                "explicit scalar wide backend used AVX-512IFMA");
        require(scalar_wide_result.survivors == wide_result.survivors,
                "scalar and automatic wide backends disagree");

        if (hardware_avx512_ifma) {
            constexpr primeforge::discovery::detail::Avx512IfmaBatch moduli{
                4'294'967'311ULL,
                1'000'000'000'039ULL,
                128'000'000'000'003ULL,
                255'999'999'999'999ULL,
                (1ULL << 51U) - 1U,
                (1ULL << 51U) + 1U,
                (1ULL << 52U) - 33U,
                (1ULL << 52U) - 1U,
                17'000'000'000'001ULL,
                64'000'000'000'001ULL,
                129'000'000'000'001ULL,
                512'000'000'000'001ULL,
                1'000'000'000'000'001ULL,
                2'000'000'000'000'001ULL,
                3'000'000'000'000'001ULL,
                4'000'000'000'000'001ULL,
            };
            constexpr std::uint32_t ifma_exponent = 1'660'936U;
            primeforge::discovery::detail::Avx512IfmaBatch montgomery_ones{};
            for (std::size_t lane = 0U; lane < moduli.size(); ++lane) {
                montgomery_ones[lane] =
                    primeforge::discovery::detail::avx512_ifma_radix % moduli[lane];
            }
            const auto inverses =
                primeforge::discovery::detail::inverse_power_of_two_avx512_ifma(
                    ifma_exponent, moduli, montgomery_ones);
            for (std::size_t lane = 0U; lane < moduli.size(); ++lane) {
                const auto expected_inverse = reference_power_wide(
                    (moduli[lane] + 1U) / 2U, ifma_exponent, moduli[lane]);
                require(inverses[lane] == expected_inverse,
                        "AVX-512IFMA Montgomery inverse disagrees with portable reference");
            }

            constexpr std::array<std::uint32_t, 8U> exponent_cases{
                0U, 1U, 2U, 31U, 32U, 1'660'936U, 99'999'998U, 99'999'999U};
            std::uint64_t state = 0x8f4d'321a'77c5'901bULL;
            constexpr std::size_t differential_batches = 4'096U;
            for (std::size_t batch = 0U; batch < differential_batches; ++batch) {
                primeforge::discovery::detail::Avx512IfmaBatch random_moduli{};
                for (auto& modulus : random_moduli) {
                    state = state * 6'364'136'223'846'793'005ULL +
                            1'442'695'040'888'963'407ULL;
                    modulus = (state & ((1ULL << 52U) - 1U)) |
                              (1ULL << 32U) | 1U;
                }
                const auto exponent = exponent_cases[batch % exponent_cases.size()];
                primeforge::discovery::detail::Avx512IfmaBatch random_ones{};
                for (std::size_t lane = 0U; lane < random_moduli.size(); ++lane) {
                    random_ones[lane] =
                        primeforge::discovery::detail::avx512_ifma_radix %
                        random_moduli[lane];
                }
                const auto actual =
                    primeforge::discovery::detail::inverse_power_of_two_avx512_ifma(
                        exponent, random_moduli, random_ones);
                for (std::size_t lane = 0U; lane < random_moduli.size(); ++lane) {
                    const auto expected_inverse = reference_power_wide(
                        (random_moduli[lane] + 1U) / 2U,
                        exponent, random_moduli[lane]);
                    require(actual[lane] == expected_inverse,
                            "IFMA randomized differential disagrees with portable reference");
                }
            }
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
                  << "discovery_sieve.avx512_ifma_differential="
                  << (hardware_avx512_ifma ? "PASS" : "SKIPPED_NO_HARDWARE") << '\n'
                  << "discovery_sieve.status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "discovery_sieve_test: " << error.what() << '\n';
        return 1;
    }
}
