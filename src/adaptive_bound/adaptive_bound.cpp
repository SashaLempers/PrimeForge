// SPDX-License-Identifier: Apache-2.0

#include "primeforge/adaptive_bound/adaptive_bound.hpp"

#include "primeforge/math/mul128.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace primeforge::adaptive_bound {
namespace {

void validate(const std::span<const BoundObservation> observations) {
    if (observations.empty()) throw std::invalid_argument("bound curve is empty");
    const auto candidates = observations.front().candidate_count;
    std::uint64_t prior_bound = 0U;
    std::uint64_t prior_eliminated = 0U;
    for (const auto& observation : observations) {
        if (observation.upper_prime <= prior_bound || observation.candidate_count != candidates ||
            observation.eliminated_count < prior_eliminated ||
            observation.eliminated_count > candidates) {
            throw std::invalid_argument("bound curve is not monotonic and internally consistent");
        }
        prior_bound = observation.upper_prime;
        prior_eliminated = observation.eliminated_count;
    }
}

[[nodiscard]] std::uint64_t saturating_multiply(
    const std::uint64_t left, const std::uint64_t right) noexcept {
    if (right != 0U && left > std::numeric_limits<std::uint64_t>::max() / right) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
}

[[nodiscard]] std::uint64_t saturating_add(
    const std::uint64_t left, const std::uint64_t right) noexcept {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

[[nodiscard]] std::uint64_t projected_cost(
    const BoundObservation& observation, const std::uint64_t next_cost) noexcept {
    return saturating_add(
        observation.cumulative_sieve_nanoseconds,
        saturating_multiply(observation.survivor_count(), next_cost));
}

[[nodiscard]] std::uint64_t power_mod(
    std::uint64_t base, std::uint64_t exponent, const std::uint64_t modulus) noexcept {
    std::uint64_t result = 1U;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) result = math::multiply_mod(result, base, modulus);
        exponent >>= 1U;
        if (exponent != 0U) base = math::multiply_mod(base, base, modulus);
    }
    return result;
}

}  // namespace

std::uint64_t BoundObservation::survivor_count() const {
    if (eliminated_count > candidate_count) {
        throw std::invalid_argument("eliminations exceed candidate count");
    }
    return candidate_count - eliminated_count;
}

BoundChoice select_offline_bound(
    const std::span<const BoundObservation> observations,
    const std::uint64_t next_test_nanoseconds_per_survivor) {
    validate(observations);
    BoundChoice choice{0U, observations.front().upper_prime,
                       projected_cost(observations.front(), next_test_nanoseconds_per_survivor),
                       "MINIMUM_MEASURED_PROJECTED_TOTAL"};
    for (std::size_t index = 1U; index < observations.size(); ++index) {
        const auto cost = projected_cost(observations[index], next_test_nanoseconds_per_survivor);
        if (cost < choice.projected_total_nanoseconds) {
            choice = {index, observations[index].upper_prime, cost,
                      "MINIMUM_MEASURED_PROJECTED_TOTAL"};
        }
    }
    return choice;
}

BoundChoice select_online_bound(
    const std::span<const BoundObservation> observations,
    const std::uint64_t next_test_nanoseconds_per_survivor) {
    validate(observations);
    std::size_t selected = 0U;
    for (std::size_t index = 1U; index < observations.size(); ++index) {
        const auto prior = observations[index - 1U];
        const auto current = observations[index];
        const auto marginal_cost = current.cumulative_sieve_nanoseconds >
                                           prior.cumulative_sieve_nanoseconds
                                       ? current.cumulative_sieve_nanoseconds -
                                             prior.cumulative_sieve_nanoseconds
                                       : 0U;
        const auto additional_eliminations = current.eliminated_count - prior.eliminated_count;
        const auto avoided_cost = saturating_multiply(
            additional_eliminations, next_test_nanoseconds_per_survivor);
        if (additional_eliminations == 0U || marginal_cost >= avoided_cost) break;
        selected = index;
    }
    return {selected, observations[selected].upper_prime,
            projected_cost(observations[selected], next_test_nanoseconds_per_survivor),
            selected + 1U == observations.size() ? "ALL_BLOCKS_COST_EFFECTIVE"
                                                 : "MARGINAL_COST_NOT_JUSTIFIED"};
}

RobustComparison compare_robustly(
    const benchmark::SummaryStatistics& candidate,
    const benchmark::SummaryStatistics& fallback) noexcept {
    if (candidate.confidence_high < fallback.confidence_low) return RobustComparison::better;
    if (fallback.confidence_high < candidate.confidence_low) return RobustComparison::worse;
    return RobustComparison::inconclusive;
}

std::string to_string(const RobustComparison comparison) {
    switch (comparison) {
        case RobustComparison::better: return "BETTER";
        case RobustComparison::worse: return "WORSE";
        case RobustComparison::inconclusive: return "INCONCLUSIVE";
    }
    return "INCONCLUSIVE";
}

bool is_base2_strong_probable_prime_u64(const std::uint64_t value) noexcept {
    if (value < 2U) return false;
    for (const std::uint64_t prime : {2U, 3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U, 31U, 37U}) {
        if (value == prime) return true;
        if (value % prime == 0U) return false;
    }
    const auto exponent = value - 1U;
    const auto shifts = static_cast<unsigned int>(std::countr_zero(exponent));
    const auto odd_part = exponent >> shifts;
    auto residue = power_mod(2U, odd_part, value);
    if (residue == 1U || residue == value - 1U) return true;
    for (unsigned int index = 1U; index < shifts; ++index) {
        residue = math::multiply_mod(residue, residue, value);
        if (residue == value - 1U) return true;
        if (residue == 1U) return false;
    }
    return false;
}

}  // namespace primeforge::adaptive_bound
