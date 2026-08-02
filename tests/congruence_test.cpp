// SPDX-License-Identifier: Apache-2.0

#include "primeforge/congruence/compiler.hpp"
#include "primeforge/core/sha256.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace congruence = primeforge::congruence;

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_failure(Function&& function, const std::string& message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, message);
}

[[nodiscard]] std::vector<std::pair<congruence::CandidateIndex, std::uint64_t>> keys(
    const std::vector<congruence::Elimination>& eliminations) {
    std::vector<std::pair<congruence::CandidateIndex, std::uint64_t>> result;
    result.reserve(eliminations.size());
    for (const auto& elimination : eliminations) {
        result.emplace_back(elimination.candidate, elimination.factor);
    }
    return result;
}

[[nodiscard]] bool exact_value_is_prime(const primeforge::math::BigInteger& value) {
    if (value.is_negative() || value < primeforge::math::BigInteger{2}) return false;
    const auto small = value.to_uint64_absolute();
    return small.has_value() && primeforge::sieve::is_prime_u64(*small);
}

void verify_family(
    const congruence::AffineExponentialFamily& family,
    const std::vector<std::uint64_t>& primes,
    const congruence::CompileOptions& options,
    const primeforge::Sha256Provider& sha256,
    std::uint64_t& candidate_counter,
    std::uint64_t& elimination_counter) {
    const auto table = congruence::compile_congruences(family, primes, options, sha256);
    const auto validation = congruence::validate_compiled_table(table, sha256);
    check(validation.valid && validation.errors.empty(), "compiled table validates");
    check(validation.canonical_report_json.find("\"valid\":true") != std::string::npos,
          "canonical validation report is true");
    check(table.table_sha256.size() == 64U, "compiled table has SHA-256 identity");

    const auto optimized = congruence::apply_compiled_table(table, sha256);
    const auto reference = congruence::scalar_reference_eliminations(family, primes);
    check(keys(optimized) == keys(reference), "optimized rules equal scalar reference");

    candidate_counter += family.k.size() * family.n.size();
    elimination_counter += static_cast<std::uint64_t>(optimized.size());
    for (const auto& elimination : optimized) {
        check(congruence::reconstruct_factor(elimination) == elimination.factor,
              "factor witness reconstructs q");
        check(elimination.value.modulo(elimination.factor) == 0U, "factor divides exact value");
        check(!exact_value_is_prime(elimination.value), "zero valid prime candidates eliminated");
        check(!elimination.proof.empty(), "generated rule has proof text");
    }

    // Absolute passage criterion checked independently for every small candidate.
    for (std::uint64_t k_index = 0U; k_index < family.k.size(); ++k_index) {
        for (std::uint64_t n_index = 0U; n_index < family.n.size(); ++n_index) {
            const auto value = congruence::evaluate_exact(family, k_index, n_index);
            if (!exact_value_is_prime(value)) continue;
            const congruence::CandidateIndex candidate{k_index, n_index};
            check(std::none_of(
                      optimized.begin(), optimized.end(), [&](const congruence::Elimination& item) {
                          return item.candidate == candidate;
                      }),
                  "prime candidate survived compiled sieve");
        }
    }
}

void test_progressions() {
    const congruence::Progression progression{-10, 10, 3U};
    check(progression.size() == 7U, "progression floor-sized domain");
    check(progression.value_at(0U) == -10 && progression.value_at(6U) == 8,
          "progression values with nonunit step");
    check(progression.contains(-4) && !progression.contains(10), "progression membership");
    check(progression.index_of(5) == 5U, "progression index reconstruction");

    const congruence::Progression crossing{
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max(),
        std::uint64_t{1} << 63U};
    check(crossing.size() == 2U, "cross-sign progression size without signed overflow");
    check(crossing.value_at(1U) == 0, "cross-sign progression value");

    expect_failure(
        [] { static_cast<void>(congruence::Progression{1, 2, 0U}.size()); },
        "zero step rejected");
    expect_failure(
        [] { static_cast<void>(congruence::Progression{2, 1, 1U}.size()); },
        "reversed progression rejected");
    expect_failure(
        [] {
            static_cast<void>(congruence::Progression{
                std::numeric_limits<std::int64_t>::min(),
                std::numeric_limits<std::int64_t>::max(), 1U}.size());
        },
        "2^64-value progression rejected");
}

void test_special_cases_and_periods(
    const primeforge::Sha256Provider& sha256,
    std::uint64_t& candidate_counter,
    std::uint64_t& elimination_counter) {
    congruence::AffineExponentialFamily special;
    special.k = {-2, 8, 2U};
    special.n = {0, 8, 2U};
    special.base = 6;
    special.constant = 3;
    verify_family(special, {2U, 3U, 5U, 7U, 11U}, {1U}, sha256,
                  candidate_counter, elimination_counter);

    const auto table = congruence::compile_congruences(special, {3U, 5U}, {1U}, sha256);
    check(!table.prime_tables[0].period.base_invertible,
          "q dividing b uses special table");
    check(table.prime_tables[0].period.constant_divisible,
          "q dividing c recorded separately");
    check(std::any_of(
              table.prime_tables[0].rules.begin(), table.prime_tables[0].rules.end(),
              [](const congruence::ForbiddenRule& rule) {
                  return rule.exponent_scope ==
                         congruence::ExponentRuleScope::positive_exponents;
              }),
          "positive-exponent q-divides-base rule emitted");

    congruence::AffineExponentialFamily equal_to_factor;
    equal_to_factor.k = {2, 2, 1U};
    equal_to_factor.n = {0, 0, 1U};
    equal_to_factor.base = 2;
    equal_to_factor.constant = 0;
    const auto equality_table = congruence::compile_congruences(
        equal_to_factor, {2U}, {1U}, sha256);
    check(congruence::apply_compiled_table(equality_table, sha256).empty(),
          "candidate N exactly q is not eliminated");

    congruence::AffineExponentialFamily nonminimal;
    nonminimal.k = {-9, 19, 4U};
    nonminimal.n = {1, 20, 3U};
    nonminimal.base = -2;
    nonminimal.constant = -5;
    nonminimal.k_parity = congruence::ParityConstraint::odd;
    verify_family(nonminimal, {3U, 5U, 7U, 11U, 13U}, {3U}, sha256,
                  candidate_counter, elimination_counter);
    const auto nonminimal_table = congruence::compile_congruences(
        nonminimal, {5U}, {3U}, sha256);
    check(nonminimal_table.prime_tables[0].period.selected_period ==
              nonminimal_table.prime_tables[0].period.multiplicative_order * 3U,
          "deliberately nonminimal period retained");
}

void test_exhaustive_small_domains(
    const primeforge::Sha256Provider& sha256,
    std::uint64_t& candidate_counter,
    std::uint64_t& elimination_counter) {
    const std::vector<std::uint64_t> primes{
        2U, 3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U, 31U, 37U, 41U, 43U};
    std::uint64_t family_index = 0U;
    for (std::int64_t base = -5; base <= 5; ++base) {
        for (std::int64_t constant = -5; constant <= 5; ++constant) {
            for (std::uint64_t k_step = 1U; k_step <= 3U; ++k_step) {
                for (std::uint64_t n_step = 1U; n_step <= 3U; ++n_step) {
                    congruence::AffineExponentialFamily family;
                    family.k = {-12, 12, k_step};
                    family.n = {0, 8, n_step};
                    family.base = base;
                    family.constant = constant;
                    family.k_parity = static_cast<congruence::ParityConstraint>(family_index % 3U);
                    family.n_parity = static_cast<congruence::ParityConstraint>((family_index / 3U) % 3U);
                    verify_family(family, primes, {1U}, sha256,
                                  candidate_counter, elimination_counter);
                    ++family_index;
                }
            }
        }
    }
}

void test_fixed_seed_fuzz(
    const primeforge::Sha256Provider& sha256,
    std::uint64_t& candidate_counter,
    std::uint64_t& elimination_counter) {
    std::mt19937_64 generator{0x5052494d45464f52ULL};
    const std::vector<std::uint64_t> primes{
        2U, 3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U};
    constexpr std::uint64_t fuzz_cases = 2'000U;
    for (std::uint64_t iteration = 0U; iteration < fuzz_cases; ++iteration) {
        const auto k_minimum = static_cast<std::int64_t>(generator() % 41U) - 20;
        const auto n_minimum = static_cast<std::int64_t>(generator() % 4U);
        congruence::AffineExponentialFamily family;
        family.k = {
            k_minimum,
            k_minimum + static_cast<std::int64_t>(generator() % 31U),
            generator() % 5U + 1U};
        family.n = {
            n_minimum,
            n_minimum + static_cast<std::int64_t>(generator() % 11U),
            generator() % 4U + 1U};
        family.base = static_cast<std::int64_t>(generator() % 21U) - 10;
        family.constant = static_cast<std::int64_t>(generator() % 41U) - 20;
        family.k_parity = static_cast<congruence::ParityConstraint>(generator() % 3U);
        family.n_parity = static_cast<congruence::ParityConstraint>(generator() % 3U);
        const congruence::CompileOptions options{iteration % 7U == 0U ? 2U : 1U};
        verify_family(family, primes, options, sha256,
                      candidate_counter, elimination_counter);
    }
}

void test_mutation_detection(const primeforge::Sha256Provider& sha256) {
    congruence::AffineExponentialFamily family;
    family.k = {1, 30, 1U};
    family.n = {0, 12, 1U};
    family.base = 2;
    family.constant = 1;
    const auto original = congruence::compile_congruences(
        family, {3U, 5U, 7U, 11U}, {1U}, sha256);
    check(!original.prime_tables.front().rules.empty(), "mutation fixture has rules");

    auto corrupted = original;
    auto& rule = corrupted.prime_tables.front().rules.front();
    rule.k_index_residue = (rule.k_index_residue + 1U) % rule.k_index_modulus;
    auto report = congruence::validate_compiled_table(corrupted, sha256);
    check(!report.valid && std::find(
              report.errors.begin(), report.errors.end(), "TABLE_HASH_MISMATCH") != report.errors.end(),
          "raw table mutation detected by SHA-256");

    corrupted.table_sha256 = congruence::compiled_table_sha256(corrupted, sha256);
    report = congruence::validate_compiled_table(corrupted, sha256);
    check(!report.valid && std::find(
              report.errors.begin(), report.errors.end(), "TABLE_SEMANTIC_MISMATCH") != report.errors.end(),
          "mutation with recomputed hash detected by semantic recompile");

    const auto eliminations = congruence::apply_compiled_table(original, sha256);
    check(!eliminations.empty(), "witness mutation fixture eliminates composites");
    auto bad_witness = eliminations.front();
    bad_witness.factor = 4U;
    expect_failure(
        [&bad_witness] { static_cast<void>(congruence::reconstruct_factor(bad_witness)); },
        "composite witness rejected");

    expect_failure(
        [&family, &sha256] {
            static_cast<void>(congruence::compile_congruences(family, {2U, 9U}, {1U}, sha256));
        },
        "composite q rejected");
    expect_failure(
        [&family, &sha256] {
            static_cast<void>(congruence::compile_congruences(family, {3U}, {0U}, sha256));
        },
        "zero period multiplier rejected");
}

void test_canonical_golden(const primeforge::Sha256Provider& sha256) {
    congruence::AffineExponentialFamily family;
    family.k = {1, 31, 2U};
    family.n = {0, 12, 2U};
    family.base = 2;
    family.constant = 1;
    family.k_parity = congruence::ParityConstraint::odd;
    const auto table = congruence::compile_congruences(
        family, {3U, 5U, 7U}, {2U}, sha256);
    const auto canonical = congruence::canonical_compiled_table_without_hash(table);
    check(canonical.size() == 4'235U, "canonical table golden byte length");
    check(table.table_sha256 ==
              "6a8409d912d12478a439267cdea662776d765ecbb90e79488c15fe7fc9f4e20b",
          "independently calculated canonical table SHA-256");
}

}  // namespace

int main() {
    try {
        const primeforge::PortableSha256Provider sha256;
        std::uint64_t candidate_counter = 0U;
        std::uint64_t elimination_counter = 0U;
        test_progressions();
        test_special_cases_and_periods(sha256, candidate_counter, elimination_counter);
        test_exhaustive_small_domains(sha256, candidate_counter, elimination_counter);
        test_fixed_seed_fuzz(sha256, candidate_counter, elimination_counter);
        test_mutation_detection(sha256);
        test_canonical_golden(sha256);
        std::cout << "PrimeForge congruence compiler tests: PASS\n"
                  << "candidate_assignments_checked=" << candidate_counter << '\n'
                  << "proper_factor_eliminations_checked=" << elimination_counter << '\n'
                  << "false_prime_eliminations=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge congruence compiler tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
