// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/family/family.hpp"
#include "primeforge/math/big_integer.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using primeforge::family::Assignment;
using primeforge::family::FamilyError;
using primeforge::family::parse_family;
using primeforge::math::BigInteger;

void check(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_family_error(Function&& function, const std::string_view expected_code) {
    try {
        function();
    } catch (const FamilyError& error) {
        check(error.code() == expected_code,
              "expected " + std::string{expected_code} + ", received " + error.code());
        check(error.line() != 0U && error.column() != 0U, "error location is explicit");
        return;
    }
    throw std::runtime_error("expected family error " + std::string{expected_code});
}

void test_big_integer() {
    check(BigInteger::from_decimal("0").to_decimal() == "0", "zero decimal round trip");
    check(BigInteger::from_decimal("-9223372036854775808").to_decimal() ==
              "-9223372036854775808",
          "signed minimum decimal round trip");
    check((BigInteger::from_decimal("18446744073709551615") + BigInteger{1}).to_decimal() ==
              "18446744073709551616",
          "addition across uint64 boundary");
    check((BigInteger::from_decimal("123456789012345678901234567890") *
           BigInteger::from_decimal("98765432109876543210987654321"))
                  .to_decimal() ==
              "12193263113702179522618503273362292333223746380111126352690",
          "independently calculated multi-limb product");
    check(primeforge::math::power(BigInteger{2}, 256U).to_decimal() ==
              "115792089237316195423570985008687907853269984665640564039457584007913129639936",
          "2^256 exact value");
    check((BigInteger{-7} < BigInteger{-3}) && (BigInteger{-3} < BigInteger{0}) &&
              (BigInteger{0} < BigInteger{4}),
          "signed total ordering");
    check(BigInteger{-17}.modulo(5U) == 3U, "canonical modulo of negative value");

    for (const auto invalid : {"", "+1", "01", "-0", "1x"}) {
        bool rejected = false;
        try {
            static_cast<void>(BigInteger::from_decimal(invalid));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "noncanonical decimal rejected");
    }
}

[[nodiscard]] std::string_view family_source_a() {
    return R"(family proth.v1;
param k in [1,1023];
param n in [1,31];
allow_product k;
value k*2^n+1;
constraint odd(k);
constraint gcd(k,2)==1;
constraint compare(k,<,2^n);
constraint congruent(k,2,1);
objective primality;
proof prove_if_survives;
)";
}

[[nodiscard]] std::string_view family_source_b() {
    return R"(# Planned equivalent spelling: reordered declarations and commutative operands.
proof prove_if_survives;
constraint congruent(k,2,1);
param n in [1,31];
objective primality;
value 1 + 2^n * k;
family proth.v1;
constraint compare(k,<,2^n);
allow_product k;
constraint gcd(2,k)==1;
param k in [1,1023];
constraint odd(k);
constraint odd(k);
)";
}

void test_canonicalization_and_constraints() {
    const auto first = parse_family(family_source_a());
    const auto second = parse_family(family_source_b());
    const auto first_canonical = primeforge::family::canonical_family(first);
    const auto second_canonical = primeforge::family::canonical_family(second);
    const std::string expected_canonical =
        "{\"allowed_product_parameters\":[\"k\"],\"constraints\":[\"CMP(<,P(k),W(C(2),P(n)))\","
        "\"CONG(P(k),2,1)\",\"GCD1(C(2),P(k))\",\"ODD(P(k))\"],\"family_id\":\"proth.v1\","
        "\"objective\":\"PRIMALITY\",\"parameters\":[{\"maximum\":\"1023\",\"minimum\":\"1\","
        "\"name\":\"k\"},{\"maximum\":\"31\",\"minimum\":\"1\",\"name\":\"n\"}],"
        "\"proof_policy\":\"PROVE_IF_SURVIVES\",\"value\":\"A(C(1),M(P(k),W(C(2),P(n))))\"}";
    check(first_canonical == second_canonical, "planned equivalent writings canonicalize identically");
    check(first_canonical == expected_canonical, "canonical family golden bytes");
    check(first_canonical.find('\n') == std::string::npos, "canonical form has no final newline");
    check(first_canonical.find(' ') == std::string::npos, "canonical form has no unnecessary spaces");
    check(first_canonical.starts_with("{\"allowed_product_parameters\":"),
          "canonical keys have deterministic ordering");

    const primeforge::PortableSha256Provider provider;
    const auto first_hash = primeforge::family::canonical_family_sha256(first, provider);
    const auto second_hash = primeforge::family::canonical_family_sha256(second, provider);
    check(first_hash == second_hash && first_hash.size() == 64U,
          "planned equivalent writings have identical SHA-256");
    check(first_hash == "82b8d4092dc444268cf0fb4376ccec502a1f3197aebcead837176d887e1c31cb",
          "independently calculated canonical SHA-256");

    check(primeforge::family::evaluate_exact(first, Assignment{{"k", 3}, {"n", 5}}).to_decimal() ==
              "97",
          "exact family evaluation");
    check(primeforge::family::evaluate_modulo(first, Assignment{{"k", 3}, {"n", 5}}, 11U) == 9U,
          "modular family evaluation");
    check(primeforge::family::constraints_satisfied(first, Assignment{{"k", 3}, {"n", 5}}),
          "all constraints accepted");
    check(!primeforge::family::constraints_satisfied(first, Assignment{{"k", 5}, {"n", 2}}),
          "comparison constraint rejected");
}

void test_domain_rejections_before_computation() {
    expect_family_error(
        [] { static_cast<void>(parse_family(
            "family x; param n in [4,1]; value n; objective primality; proof none;")); },
        "DOMAIN_REVERSED");
    expect_family_error(
        [] { static_cast<void>(parse_family(
            "family x; param n in [-1,4]; value 2^n; objective primality; proof none;")); },
        "POWER_EXPONENT_DOMAIN");
    expect_family_error(
        [] { static_cast<void>(parse_family(
            "family x; param a in [1,4]; param b in [1,4]; value a*b; objective primality; proof none;")); },
        "MULTIPLICATION_NOT_AUTHORIZED");
    expect_family_error(
        [] { static_cast<void>(parse_family(
            "family x; param n in [1,4]; value arbitrary(n); objective primality; proof none;")); },
        "PARSE_EXPECTED_SEMICOLON");
    expect_family_error(
        [] { static_cast<void>(parse_family(
            "family x; param n in [1,4]; value 1.5+n; objective primality; proof none;")); },
        "LEX_INVALID_CHARACTER");
    expect_family_error(
        [] { static_cast<void>(parse_family(
            "family x; param n in [1,4]; value n; constraint congruent(n,0,0); objective primality; proof none;")); },
        "CONGRUENCE_DOMAIN");

    const auto huge = parse_family(
        "family huge; param n in [0,6000000]; value 4^n; objective primality; proof none;");
    check(primeforge::family::estimate_maximum_bits(huge) > 10'000'000U,
          "large bound estimated without constructing N");
    expect_family_error(
        [&huge] { static_cast<void>(primeforge::family::evaluate_exact(huge, Assignment{{"n", 0}})); },
        "VALUE_SIZE_LIMIT");

    const auto huge_constraint = parse_family(
        "family huge.constraint; param n in [0,6000000]; value n; "
        "constraint compare(4^n,>,0); objective primality; proof none;");
    expect_family_error(
        [&huge_constraint] {
            static_cast<void>(primeforge::family::constraints_satisfied(
                huge_constraint, Assignment{{"n", 0}}));
        },
        "CONSTRAINT_SIZE_LIMIT");

    const auto bounded = parse_family(
        "family bounded; param n in [1,4]; value n+1; objective primality; proof none;");
    expect_family_error(
        [&bounded] {
            static_cast<void>(primeforge::family::evaluate_exact(bounded, Assignment{{"n", 5}}));
        },
        "ASSIGNMENT_OUT_OF_BOUNDS");
    expect_family_error(
        [&bounded] {
            static_cast<void>(primeforge::family::evaluate_modulo(
                bounded, Assignment{{"n", 1}}, 0U));
        },
        "MODULUS_ZERO");
}

void test_two_million_exact_modular_agreements() {
    const auto definition = parse_family(
        "family concordance.v1; param a in [-1000,1000]; param b in [-500,500]; "
        "value a*3+b-7; objective primality; proof none;");
    Assignment assignment{{"a", 0}, {"b", 0}};
    constexpr std::uint64_t case_count = 2'000'000U;
    for (std::uint64_t index = 0U; index < case_count; ++index) {
        assignment["a"] = static_cast<std::int64_t>(index % 2'001U) - 1'000;
        assignment["b"] = static_cast<std::int64_t>((index * 48'271U) % 1'001U) - 500;
        const auto modulus = index % 65'519U + 2U;
        const auto exact = primeforge::family::evaluate_exact(definition, assignment);
        const auto modular = primeforge::family::evaluate_modulo(definition, assignment, modulus);
        check(exact.modulo(modulus) == modular,
              "exact/modular mismatch at case " + std::to_string(index));
    }
}

}  // namespace

int main() {
    try {
        test_big_integer();
        test_canonicalization_and_constraints();
        test_domain_rejections_before_computation();
        test_two_million_exact_modular_agreements();
        std::cout << "PrimeForge family-language tests: PASS (2000000 exact/modular cases)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge family-language tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
