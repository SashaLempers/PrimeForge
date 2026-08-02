// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/benchmark/benchmark.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace primeforge::adaptive_bound {

struct BoundObservation {
    std::uint64_t upper_prime{};
    std::uint64_t cumulative_sieve_nanoseconds{};
    std::uint64_t candidate_count{};
    std::uint64_t eliminated_count{};

    [[nodiscard]] std::uint64_t survivor_count() const;
};

struct BoundChoice {
    std::size_t observation_index{};
    std::uint64_t upper_prime{};
    std::uint64_t projected_total_nanoseconds{};
    std::string reason;
};

enum class RobustComparison { better, worse, inconclusive };

// Uses measured survivor counts. It deliberately contains no 1/q selectivity model.
[[nodiscard]] BoundChoice select_offline_bound(
    std::span<const BoundObservation> observations,
    std::uint64_t next_test_nanoseconds_per_survivor);

// Applies the marginal stop rule to a cumulative observation curve.
[[nodiscard]] BoundChoice select_online_bound(
    std::span<const BoundObservation> observations,
    std::uint64_t next_test_nanoseconds_per_survivor);

[[nodiscard]] RobustComparison compare_robustly(
    const benchmark::SummaryStatistics& candidate,
    const benchmark::SummaryStatistics& fallback) noexcept;

[[nodiscard]] std::string to_string(RobustComparison comparison);

// Measurement-only PRP workload. A true result is PROBABLE_PRIME, never PROVEN_PRIME.
[[nodiscard]] bool is_base2_strong_probable_prime_u64(std::uint64_t value) noexcept;

}  // namespace primeforge::adaptive_bound
