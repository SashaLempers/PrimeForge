// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"
#include "primeforge/math/big_integer.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace primeforge::family {

class FamilyError final : public std::runtime_error {
public:
    FamilyError(std::string code, std::size_t line, std::size_t column, std::string message);

    [[nodiscard]] const std::string& code() const noexcept;
    [[nodiscard]] std::size_t line() const noexcept;
    [[nodiscard]] std::size_t column() const noexcept;

private:
    std::string code_;
    std::size_t line_{};
    std::size_t column_{};
};

struct Parameter {
    std::string name;
    std::int64_t minimum{};
    std::int64_t maximum{};
};

enum class ExpressionKind {
    constant,
    parameter,
    add,
    subtract,
    multiply,
    negate,
    power
};

struct Expression;
using ExpressionPtr = std::shared_ptr<const Expression>;

struct Expression {
    ExpressionKind kind{ExpressionKind::constant};
    math::BigInteger constant;
    std::string parameter;
    ExpressionPtr left;
    ExpressionPtr right;
};

enum class ConstraintKind {
    gcd_equals_one,
    even,
    odd,
    comparison,
    congruence
};

enum class ComparisonOperator {
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal
};

struct Constraint {
    ConstraintKind kind{ConstraintKind::comparison};
    ExpressionPtr left;
    ExpressionPtr right;
    ComparisonOperator comparison{ComparisonOperator::equal};
    std::uint64_t modulus{};
    std::uint64_t residue{};
};

struct FamilyDefinition {
    std::string family_id;
    std::vector<Parameter> parameters;
    std::vector<std::string> allowed_product_parameters;
    ExpressionPtr value;
    std::vector<Constraint> constraints;
    std::string objective;
    std::string proof_policy;
    // Derived, non-serialized safety metadata populated by the parser. A zero
    // value means that a programmatically constructed definition has not been
    // analysed yet.
    std::uint64_t maximum_bits_estimate{};
    std::uint64_t maximum_exact_constraint_bits_estimate{};
};

using Assignment = std::unordered_map<std::string, std::int64_t>;

[[nodiscard]] FamilyDefinition parse_family(std::string_view source);
[[nodiscard]] std::string canonical_family(const FamilyDefinition& definition);
[[nodiscard]] std::string canonical_family_sha256(
    const FamilyDefinition& definition, const Sha256Provider& provider);
[[nodiscard]] std::uint64_t estimate_maximum_bits(const FamilyDefinition& definition);
[[nodiscard]] math::BigInteger evaluate_exact(
    const FamilyDefinition& definition, const Assignment& assignment);
[[nodiscard]] std::uint64_t evaluate_modulo(
    const FamilyDefinition& definition, const Assignment& assignment, std::uint64_t modulus);
[[nodiscard]] bool constraints_satisfied(
    const FamilyDefinition& definition, const Assignment& assignment);

}  // namespace primeforge::family
