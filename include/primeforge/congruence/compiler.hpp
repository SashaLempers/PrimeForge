// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"
#include "primeforge/math/big_integer.hpp"

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace primeforge::congruence {

enum class ParityConstraint {
    any,
    even,
    odd
};

struct Progression {
    std::int64_t minimum{};
    std::int64_t maximum{};
    std::uint64_t step{1U};

    [[nodiscard]] std::uint64_t size() const;
    [[nodiscard]] std::int64_t value_at(std::uint64_t index) const;
    [[nodiscard]] bool contains(std::int64_t value) const;
    [[nodiscard]] std::uint64_t index_of(std::int64_t value) const;
    [[nodiscard]] bool operator==(const Progression&) const = default;
};

struct AffineExponentialFamily {
    Progression k;
    Progression n;
    std::int64_t base{};
    std::int64_t constant{};
    ParityConstraint k_parity{ParityConstraint::any};
    ParityConstraint n_parity{ParityConstraint::any};

    [[nodiscard]] bool operator==(const AffineExponentialFamily&) const = default;
};

struct CompileOptions {
    // Any positive multiplier is correct. One selects the minimal order;
    // larger values deliberately exercise non-minimal periods.
    std::uint64_t period_multiplier{1U};

    [[nodiscard]] bool operator==(const CompileOptions&) const = default;
};

struct PeriodEntry {
    std::uint64_t prime{};
    bool base_invertible{};
    bool constant_divisible{};
    std::uint64_t multiplicative_order{};
    std::uint64_t selected_period{};
    std::uint64_t exponent_index_period{};

    [[nodiscard]] bool operator==(const PeriodEntry&) const = default;
};

enum class ExponentRuleScope {
    index_congruence,
    exact_exponent,
    positive_exponents
};

struct ForbiddenRule {
    std::uint64_t prime{};
    ExponentRuleScope exponent_scope{ExponentRuleScope::index_congruence};
    std::uint64_t exponent_index_residue{};
    std::uint64_t exponent_index_modulus{1U};
    std::int64_t exact_exponent{};
    std::uint64_t k_index_residue{};
    std::uint64_t k_index_modulus{1U};
    std::uint64_t forbidden_k_residue{};
    std::uint64_t power_residue{};
    std::string proof;

    [[nodiscard]] bool operator==(const ForbiddenRule&) const = default;
};

struct PrimeRuleTable {
    PeriodEntry period;
    // Each item is a compressed Cartesian class over n-index and k-index.
    std::vector<ForbiddenRule> rules;

    [[nodiscard]] bool operator==(const PrimeRuleTable&) const = default;
};

struct CompiledTable {
    std::string format_version{"primeforge-congruence-v1"};
    AffineExponentialFamily family;
    CompileOptions options;
    std::vector<std::uint64_t> primes;
    std::vector<PrimeRuleTable> prime_tables;
    std::string table_sha256;

    [[nodiscard]] bool operator==(const CompiledTable&) const = default;
};

struct CandidateIndex {
    std::uint64_t k_index{};
    std::uint64_t n_index{};

    [[nodiscard]] auto operator<=>(const CandidateIndex&) const = default;
};

struct Elimination {
    CandidateIndex candidate;
    std::uint64_t factor{};
    math::BigInteger value;
    std::string proof;
};

struct TableValidationReport {
    bool valid{};
    std::vector<std::string> errors;
    std::uint64_t rule_count{};
    std::string canonical_report_json;
};

[[nodiscard]] CompiledTable compile_congruences(
    const AffineExponentialFamily& family,
    std::vector<std::uint64_t> primes,
    const CompileOptions& options,
    const Sha256Provider& sha256);

[[nodiscard]] std::string canonical_compiled_table_without_hash(const CompiledTable& table);
[[nodiscard]] std::string compiled_table_sha256(
    const CompiledTable& table, const Sha256Provider& sha256);
[[nodiscard]] TableValidationReport validate_compiled_table(
    const CompiledTable& table, const Sha256Provider& sha256);

[[nodiscard]] math::BigInteger evaluate_exact(
    const AffineExponentialFamily& family, std::uint64_t k_index, std::uint64_t n_index);
[[nodiscard]] std::uint64_t evaluate_modulo(
    const AffineExponentialFamily& family,
    std::uint64_t k_index,
    std::uint64_t n_index,
    std::uint64_t modulus);

[[nodiscard]] std::vector<Elimination> apply_compiled_table(
    const CompiledTable& table, const Sha256Provider& sha256);
[[nodiscard]] std::vector<Elimination> scalar_reference_eliminations(
    const AffineExponentialFamily& family, const std::vector<std::uint64_t>& primes);

// Revalidates the local proof obligations before returning the witness.
[[nodiscard]] std::uint64_t reconstruct_factor(const Elimination& elimination);

}  // namespace primeforge::congruence
