// SPDX-License-Identifier: Apache-2.0

#include "primeforge/congruence/compiler.hpp"

#include "primeforge/math/mul128.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace primeforge::congruence {
namespace {

constexpr std::uint64_t maximum_exact_bits = 10'000'000U;

[[nodiscard]] std::uint64_t unsigned_difference(
    const std::int64_t larger, const std::int64_t smaller) noexcept {
    return static_cast<std::uint64_t>(larger) - static_cast<std::uint64_t>(smaller);
}

[[nodiscard]] std::int64_t add_unsigned_offset(
    const std::int64_t value, const std::uint64_t offset) {
    if (value >= 0) {
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - value)) {
            throw std::overflow_error("progression value exceeds int64 maximum");
        }
        return value + static_cast<std::int64_t>(offset);
    }
    const auto negative_magnitude = std::uint64_t{0} - static_cast<std::uint64_t>(value);
    if (offset < negative_magnitude) {
        const auto remaining = negative_magnitude - offset;
        if (remaining == (std::uint64_t{1} << 63U)) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return -static_cast<std::int64_t>(remaining);
    }
    const auto positive = offset - negative_magnitude;
    if (positive > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("progression value exceeds int64 maximum");
    }
    return static_cast<std::int64_t>(positive);
}

void validate_family(const AffineExponentialFamily& family) {
    static_cast<void>(family.k.size());
    static_cast<void>(family.n.size());
    if (family.n.minimum < 0) {
        throw std::invalid_argument("exponent progression must be nonnegative");
    }
}

[[nodiscard]] bool parity_matches(
    const std::int64_t value, const ParityConstraint constraint) noexcept {
    if (constraint == ParityConstraint::any) return true;
    const bool odd = (static_cast<std::uint64_t>(value) & 1U) != 0U;
    return constraint == ParityConstraint::odd ? odd : !odd;
}

[[nodiscard]] std::uint64_t normalize_modulo(
    const std::int64_t value, const std::uint64_t modulus) {
    return math::BigInteger{value}.modulo(modulus);
}

[[nodiscard]] std::uint64_t add_modulo(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::uint64_t modulus) noexcept {
    return left >= modulus - right ? left - (modulus - right) : left + right;
}

[[nodiscard]] std::uint64_t subtract_modulo(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::uint64_t modulus) noexcept {
    return left >= right ? left - right : modulus - (right - left);
}

[[nodiscard]] std::uint64_t power_modulo(
    std::uint64_t base, std::uint64_t exponent, const std::uint64_t modulus) {
    std::uint64_t result = 1U % modulus;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = math::multiply_mod(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = math::multiply_mod(base, base, modulus);
        }
    }
    return result;
}

[[nodiscard]] std::uint64_t modular_inverse_prime(
    const std::uint64_t value, const std::uint64_t prime) {
    if (value == 0U || !sieve::is_prime_u64(prime)) {
        throw std::invalid_argument("inverse requires a nonzero residue modulo a prime");
    }
    return power_modulo(value, prime - 2U, prime);
}

[[nodiscard]] std::uint64_t multiplicative_order(
    const std::uint64_t base, const std::uint64_t prime) {
    if (std::gcd(base, prime) != 1U) {
        throw std::invalid_argument("multiplicative order requires coprime operands");
    }
    std::uint64_t order = prime - 1U;
    auto remaining = order;
    for (std::uint64_t divisor = 2U; divisor <= remaining / divisor; ++divisor) {
        if (remaining % divisor != 0U) continue;
        while (remaining % divisor == 0U) remaining /= divisor;
        while (order % divisor == 0U &&
               power_modulo(base, order / divisor, prime) == 1U) {
            order /= divisor;
        }
    }
    if (remaining > 1U) {
        while (order % remaining == 0U &&
               power_modulo(base, order / remaining, prime) == 1U) {
            order /= remaining;
        }
    }
    return order;
}

[[nodiscard]] std::string parity_text(const ParityConstraint constraint) {
    switch (constraint) {
        case ParityConstraint::any: return "ANY";
        case ParityConstraint::even: return "EVEN";
        case ParityConstraint::odd: return "ODD";
    }
    throw std::logic_error("unknown parity constraint");
}

[[nodiscard]] std::string scope_text(const ExponentRuleScope scope) {
    switch (scope) {
        case ExponentRuleScope::index_congruence: return "INDEX_CONGRUENCE";
        case ExponentRuleScope::exact_exponent: return "EXACT_EXPONENT";
        case ExponentRuleScope::positive_exponents: return "POSITIVE_EXPONENTS";
    }
    throw std::logic_error("unknown exponent rule scope");
}

[[nodiscard]] std::string quote_json(const std::string_view value) {
    std::string result{"\""};
    for (const char character : value) {
        if (character == '\"') result += "\\\"";
        else if (character == '\\') result += "\\\\";
        else if (static_cast<unsigned char>(character) < 0x20U ||
                 static_cast<unsigned char>(character) >= 0x80U) {
            throw std::invalid_argument("congruence canonical strings must be printable ASCII");
        } else {
            result.push_back(character);
        }
    }
    result.push_back('\"');
    return result;
}

[[nodiscard]] std::string bool_json(const bool value) {
    return value ? "true" : "false";
}

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string_view value) noexcept {
    return std::as_bytes(std::span{value.data(), value.size()});
}

[[nodiscard]] std::string progression_json(const Progression& progression) {
    return "{\"maximum\":" + quote_json(std::to_string(progression.maximum)) +
           ",\"minimum\":" + quote_json(std::to_string(progression.minimum)) +
           ",\"step\":" + quote_json(std::to_string(progression.step)) + "}";
}

[[nodiscard]] std::string family_json(const AffineExponentialFamily& family) {
    return "{\"base\":" + quote_json(std::to_string(family.base)) +
           ",\"constant\":" + quote_json(std::to_string(family.constant)) +
           ",\"k\":" + progression_json(family.k) +
           ",\"k_parity\":" + quote_json(parity_text(family.k_parity)) +
           ",\"n\":" + progression_json(family.n) +
           ",\"n_parity\":" + quote_json(parity_text(family.n_parity)) + "}";
}

[[nodiscard]] std::string rule_proof(
    const ForbiddenRule& rule, const PeriodEntry& period) {
    std::string result = "q=" + std::to_string(rule.prime) + " is prime; ";
    if (rule.exponent_scope == ExponentRuleScope::index_congruence) {
        result += "n_index=" + std::to_string(rule.exponent_index_residue) + " mod " +
                  std::to_string(rule.exponent_index_modulus);
    } else if (rule.exponent_scope == ExponentRuleScope::exact_exponent) {
        result += "n=" + std::to_string(rule.exact_exponent);
    } else {
        result += "n>0";
    }
    result += "; b^n mod q=" + std::to_string(rule.power_residue);
    if (!period.base_invertible && period.constant_divisible &&
        rule.exponent_scope == ExponentRuleScope::positive_exponents) {
        result += "; b^n=0 and c=0 mod q, so every k is forbidden";
    } else {
        result += "; k mod q=" + std::to_string(rule.forbidden_k_residue) +
                  "; k_index=" + std::to_string(rule.k_index_residue) + " mod " +
                  std::to_string(rule.k_index_modulus);
    }
    result += "; F mod q=0";
    if (!period.base_invertible) result += "; q divides b handled separately";
    if (period.constant_divisible) result += "; q divides c";
    result += "; elimination additionally requires abs(F)>q and valid-candidate semantics";
    return result;
}

[[nodiscard]] bool k_index_class(
    const Progression& k,
    const std::uint64_t target,
    const std::uint64_t prime,
    std::uint64_t& index_residue,
    std::uint64_t& index_modulus) {
    const auto step = k.step % prime;
    const auto minimum = normalize_modulo(k.minimum, prime);
    if (step == 0U) {
        if (minimum != target) return false;
        index_residue = 0U;
        index_modulus = 1U;
        return true;
    }
    index_residue = math::multiply_mod(
        subtract_modulo(target, minimum, prime), modular_inverse_prime(step, prime), prime);
    index_modulus = prime;
    return index_residue < k.size();
}

void add_rule(
    PrimeRuleTable& table,
    const AffineExponentialFamily& family,
    const ExponentRuleScope scope,
    const std::uint64_t exponent_index_residue,
    const std::uint64_t exponent_index_modulus,
    const std::int64_t exact_exponent,
    const std::uint64_t target_k,
    const std::uint64_t power_residue) {
    ForbiddenRule rule;
    rule.prime = table.period.prime;
    rule.exponent_scope = scope;
    rule.exponent_index_residue = exponent_index_residue;
    rule.exponent_index_modulus = exponent_index_modulus;
    rule.exact_exponent = exact_exponent;
    rule.forbidden_k_residue = target_k;
    rule.power_residue = power_residue;
    if (!k_index_class(
            family.k, target_k, rule.prime,
            rule.k_index_residue, rule.k_index_modulus)) {
        return;
    }
    rule.proof = rule_proof(rule, table.period);
    table.rules.push_back(std::move(rule));
}

void add_all_k_rule(
    PrimeRuleTable& table,
    const ExponentRuleScope scope,
    const std::uint64_t exponent_index_residue,
    const std::uint64_t exponent_index_modulus,
    const std::int64_t exact_exponent) {
    ForbiddenRule rule;
    rule.prime = table.period.prime;
    rule.exponent_scope = scope;
    rule.exponent_index_residue = exponent_index_residue;
    rule.exponent_index_modulus = exponent_index_modulus;
    rule.exact_exponent = exact_exponent;
    rule.k_index_residue = 0U;
    rule.k_index_modulus = 1U;
    rule.forbidden_k_residue = 0U;
    rule.power_residue = 0U;
    rule.proof = rule_proof(rule, table.period);
    table.rules.push_back(std::move(rule));
}

[[nodiscard]] PrimeRuleTable compile_prime(
    const AffineExponentialFamily& family,
    const std::uint64_t prime,
    const CompileOptions& options) {
    PrimeRuleTable table;
    table.period.prime = prime;
    const auto base = normalize_modulo(family.base, prime);
    const auto constant = normalize_modulo(family.constant, prime);
    table.period.base_invertible = base != 0U;
    table.period.constant_divisible = constant == 0U;

    if (base == 0U) {
        // b^0 = 1, including the conventional 0^0 used by integer power.
        if (family.n.contains(0)) {
            const auto target = constant == 0U ? 0U : prime - constant;
            add_rule(
                table, family, ExponentRuleScope::exact_exponent,
                0U, 1U, 0, target, 1U);
        }
        if (family.n.maximum > 0 && constant == 0U) {
            add_all_k_rule(
                table, ExponentRuleScope::positive_exponents, 0U, 1U, 0);
        }
        return table;
    }

    table.period.multiplicative_order = multiplicative_order(base, prime);
    if (options.period_multiplier == 0U ||
        table.period.multiplicative_order >
            std::numeric_limits<std::uint64_t>::max() / options.period_multiplier) {
        throw std::invalid_argument("period multiplier is zero or overflows");
    }
    table.period.selected_period =
        table.period.multiplicative_order * options.period_multiplier;
    const auto step_modulo_period = family.n.step % table.period.selected_period;
    table.period.exponent_index_period =
        table.period.selected_period /
        std::gcd(table.period.selected_period, step_modulo_period);
    const auto index_period = table.period.exponent_index_period;
    const auto n_count = family.n.size();
    const auto minimum_modulo_period =
        static_cast<std::uint64_t>(family.n.minimum) % table.period.selected_period;
    for (std::uint64_t index = 0U; index < index_period && index < n_count; ++index) {
        const auto offset = math::multiply_mod(
            index % table.period.selected_period,
            step_modulo_period,
            table.period.selected_period);
        const auto exponent = add_modulo(
            minimum_modulo_period, offset, table.period.selected_period);
        const auto power = power_modulo(base, exponent, prime);
        const auto negative_constant = constant == 0U ? 0U : prime - constant;
        const auto target = math::multiply_mod(
            negative_constant, modular_inverse_prime(power, prime), prime);
        add_rule(
            table, family, ExponentRuleScope::index_congruence,
            index, index_period, 0, target, power);
    }
    return table;
}

[[nodiscard]] std::uint64_t estimate_exact_bits(
    const AffineExponentialFamily& family, const std::uint64_t n_index) {
    const auto exponent = static_cast<std::uint64_t>(family.n.value_at(n_index));
    const auto base_magnitude = family.base < 0
                                    ? std::uint64_t{0} - static_cast<std::uint64_t>(family.base)
                                    : static_cast<std::uint64_t>(family.base);
    std::uint64_t power_bits = 1U;
    if (base_magnitude > 1U && exponent != 0U) {
        const auto ceiling_log2 = static_cast<std::uint64_t>(
            std::bit_width(base_magnitude - 1U));
        if (ceiling_log2 > (maximum_exact_bits + 1U) / exponent) {
            return maximum_exact_bits + 1U;
        }
        power_bits = ceiling_log2 * exponent + 1U;
    }
    const auto k_value = family.k.value_at(0U);
    const auto k_other = family.k.value_at(family.k.size() - 1U);
    const auto magnitude = [](const std::int64_t value) noexcept {
        return value < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                         : static_cast<std::uint64_t>(value);
    };
    const auto k_bits = static_cast<std::uint64_t>(
        std::bit_width(std::max(magnitude(k_value), magnitude(k_other))));
    return std::min(
        maximum_exact_bits + 1U,
        power_bits + std::max<std::uint64_t>(1U, k_bits) + 2U);
}

[[nodiscard]] bool valid_candidate_semantics(const math::BigInteger& value) {
    return value > math::BigInteger{1};
}

[[nodiscard]] bool proper_factor_witness(
    const math::BigInteger& value, const std::uint64_t prime) {
    if (!sieve::is_prime_u64(prime) || !valid_candidate_semantics(value) ||
        value.modulo(prime) != 0U) {
        return false;
    }
    const auto absolute = value.is_negative() ? -value : value;
    return absolute > math::BigInteger::from_decimal(std::to_string(prime));
}

template <typename Function>
void for_each_n_index(
    const ForbiddenRule& rule, const Progression& progression, Function&& function) {
    const auto count = progression.size();
    if (rule.exponent_scope == ExponentRuleScope::exact_exponent) {
        if (progression.contains(rule.exact_exponent)) {
            function(progression.index_of(rule.exact_exponent));
        }
        return;
    }
    if (rule.exponent_scope == ExponentRuleScope::positive_exponents) {
        for (std::uint64_t index = 0U; index < count; ++index) {
            if (progression.value_at(index) > 0) function(index);
        }
        return;
    }
    for (auto index = rule.exponent_index_residue; index < count;) {
        function(index);
        if (rule.exponent_index_modulus > count - 1U - index) break;
        index += rule.exponent_index_modulus;
    }
}

template <typename Function>
void for_each_k_index(
    const ForbiddenRule& rule, const Progression& progression, Function&& function) {
    const auto count = progression.size();
    for (auto index = rule.k_index_residue; index < count;) {
        function(index);
        if (rule.k_index_modulus > count - 1U - index) break;
        index += rule.k_index_modulus;
    }
}

[[nodiscard]] std::vector<std::uint64_t> normalized_primes(
    std::vector<std::uint64_t> primes) {
    std::sort(primes.begin(), primes.end());
    primes.erase(std::unique(primes.begin(), primes.end()), primes.end());
    for (const auto prime : primes) {
        if (!sieve::is_prime_u64(prime)) {
            throw std::invalid_argument("congruence modulus is not prime: " + std::to_string(prime));
        }
    }
    return primes;
}

}  // namespace

std::uint64_t Progression::size() const {
    if (step == 0U) throw std::invalid_argument("progression step must be positive");
    if (minimum > maximum) throw std::invalid_argument("progression bounds are reversed");
    const auto span = unsigned_difference(maximum, minimum);
    const auto intervals = span / step;
    if (intervals == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("progression contains 2^64 values");
    }
    return intervals + 1U;
}

std::int64_t Progression::value_at(const std::uint64_t index) const {
    const auto count = size();
    if (index >= count) throw std::out_of_range("progression index is out of range");
    if (index != 0U && step > std::numeric_limits<std::uint64_t>::max() / index) {
        throw std::overflow_error("progression offset overflows");
    }
    return add_unsigned_offset(minimum, index * step);
}

bool Progression::contains(const std::int64_t value) const {
    static_cast<void>(size());
    return value >= minimum && value <= maximum &&
           unsigned_difference(value, minimum) % step == 0U;
}

std::uint64_t Progression::index_of(const std::int64_t value) const {
    if (!contains(value)) throw std::out_of_range("value is not in progression");
    return unsigned_difference(value, minimum) / step;
}

CompiledTable compile_congruences(
    const AffineExponentialFamily& family,
    std::vector<std::uint64_t> primes,
    const CompileOptions& options,
    const Sha256Provider& sha256) {
    validate_family(family);
    if (options.period_multiplier == 0U) {
        throw std::invalid_argument("period multiplier must be positive");
    }
    CompiledTable result;
    result.family = family;
    result.options = options;
    result.primes = normalized_primes(std::move(primes));
    result.prime_tables.reserve(result.primes.size());
    for (const auto prime : result.primes) {
        result.prime_tables.push_back(compile_prime(family, prime, options));
    }
    result.table_sha256 = compiled_table_sha256(result, sha256);
    return result;
}

std::string canonical_compiled_table_without_hash(const CompiledTable& table) {
    std::string result = "{\"family\":" + family_json(table.family) +
                         ",\"format_version\":" + quote_json(table.format_version) +
                         ",\"options\":{\"period_multiplier\":" +
                         quote_json(std::to_string(table.options.period_multiplier)) +
                         "},\"prime_tables\":[";
    for (std::size_t table_index = 0U; table_index < table.prime_tables.size(); ++table_index) {
        if (table_index != 0U) result.push_back(',');
        const auto& prime_table = table.prime_tables[table_index];
        const auto& period = prime_table.period;
        result += "{\"period\":{\"base_invertible\":" + bool_json(period.base_invertible) +
                  ",\"constant_divisible\":" + bool_json(period.constant_divisible) +
                  ",\"exponent_index_period\":" +
                  quote_json(std::to_string(period.exponent_index_period)) +
                  ",\"multiplicative_order\":" +
                  quote_json(std::to_string(period.multiplicative_order)) +
                  ",\"prime\":" + quote_json(std::to_string(period.prime)) +
                  ",\"selected_period\":" + quote_json(std::to_string(period.selected_period)) +
                  "},\"rules\":[";
        for (std::size_t rule_index = 0U; rule_index < prime_table.rules.size(); ++rule_index) {
            if (rule_index != 0U) result.push_back(',');
            const auto& rule = prime_table.rules[rule_index];
            result += "{\"exact_exponent\":" + quote_json(std::to_string(rule.exact_exponent)) +
                      ",\"exponent_index_modulus\":" +
                      quote_json(std::to_string(rule.exponent_index_modulus)) +
                      ",\"exponent_index_residue\":" +
                      quote_json(std::to_string(rule.exponent_index_residue)) +
                      ",\"exponent_scope\":" + quote_json(scope_text(rule.exponent_scope)) +
                      ",\"forbidden_k_residue\":" +
                      quote_json(std::to_string(rule.forbidden_k_residue)) +
                      ",\"k_index_modulus\":" +
                      quote_json(std::to_string(rule.k_index_modulus)) +
                      ",\"k_index_residue\":" +
                      quote_json(std::to_string(rule.k_index_residue)) +
                      ",\"power_residue\":" + quote_json(std::to_string(rule.power_residue)) +
                      ",\"prime\":" + quote_json(std::to_string(rule.prime)) +
                      ",\"proof\":" + quote_json(rule.proof) + "}";
        }
        result += "]}";
    }
    result += "],\"primes\":[";
    for (std::size_t index = 0U; index < table.primes.size(); ++index) {
        if (index != 0U) result.push_back(',');
        result += quote_json(std::to_string(table.primes[index]));
    }
    result += "]}";
    return result;
}

std::string compiled_table_sha256(
    const CompiledTable& table, const Sha256Provider& sha256) {
    const auto canonical = canonical_compiled_table_without_hash(table);
    return sha256_to_hex(sha256.digest(bytes_of(canonical)));
}

TableValidationReport validate_compiled_table(
    const CompiledTable& table, const Sha256Provider& sha256) {
    TableValidationReport report;
    for (const auto& prime_table : table.prime_tables) {
        report.rule_count += static_cast<std::uint64_t>(prime_table.rules.size());
    }
    if (table.format_version != "primeforge-congruence-v1") {
        report.errors.push_back("FORMAT_VERSION_UNSUPPORTED");
    }
    try {
        if (compiled_table_sha256(table, sha256) != table.table_sha256) {
            report.errors.push_back("TABLE_HASH_MISMATCH");
        }
        const auto expected = compile_congruences(
            table.family, table.primes, table.options, sha256);
        if (canonical_compiled_table_without_hash(expected) !=
            canonical_compiled_table_without_hash(table)) {
            report.errors.push_back("TABLE_SEMANTIC_MISMATCH");
        }
    } catch (const std::exception& error) {
        report.errors.push_back("TABLE_RECOMPILE_ERROR:" + std::string{error.what()});
    }
    report.valid = report.errors.empty();
    report.canonical_report_json =
        "{\"errors\":[";
    for (std::size_t index = 0U; index < report.errors.size(); ++index) {
        if (index != 0U) report.canonical_report_json.push_back(',');
        report.canonical_report_json += quote_json(report.errors[index]);
    }
    report.canonical_report_json += "],\"rule_count\":" +
                                    quote_json(std::to_string(report.rule_count)) +
                                    ",\"valid\":" + bool_json(report.valid) + "}";
    return report;
}

math::BigInteger evaluate_exact(
    const AffineExponentialFamily& family,
    const std::uint64_t k_index,
    const std::uint64_t n_index) {
    validate_family(family);
    if (estimate_exact_bits(family, n_index) > maximum_exact_bits) {
        throw std::length_error("candidate exceeds exact-evaluation safety limit");
    }
    const auto k = family.k.value_at(k_index);
    const auto n = static_cast<std::uint64_t>(family.n.value_at(n_index));
    return math::BigInteger{k} * math::power(math::BigInteger{family.base}, n) +
           math::BigInteger{family.constant};
}

std::uint64_t evaluate_modulo(
    const AffineExponentialFamily& family,
    const std::uint64_t k_index,
    const std::uint64_t n_index,
    const std::uint64_t modulus) {
    validate_family(family);
    if (modulus == 0U) throw std::invalid_argument("modulus must be nonzero");
    const auto k = normalize_modulo(family.k.value_at(k_index), modulus);
    const auto exponent = static_cast<std::uint64_t>(family.n.value_at(n_index));
    const auto power = power_modulo(normalize_modulo(family.base, modulus), exponent, modulus);
    return add_modulo(
        math::multiply_mod(k, power, modulus),
        normalize_modulo(family.constant, modulus), modulus);
}

std::vector<Elimination> apply_compiled_table(
    const CompiledTable& table, const Sha256Provider& sha256) {
    const auto validation = validate_compiled_table(table, sha256);
    if (!validation.valid) {
        throw std::invalid_argument("compiled congruence table failed validation");
    }
    std::map<CandidateIndex, Elimination> eliminated;
    for (const auto& prime_table : table.prime_tables) {
        for (const auto& rule : prime_table.rules) {
            for_each_n_index(rule, table.family.n, [&](const std::uint64_t n_index) {
                const auto n = table.family.n.value_at(n_index);
                if (!parity_matches(n, table.family.n_parity)) return;
                for_each_k_index(rule, table.family.k, [&](const std::uint64_t k_index) {
                    const auto k = table.family.k.value_at(k_index);
                    if (!parity_matches(k, table.family.k_parity)) return;
                    const CandidateIndex candidate{k_index, n_index};
                    if (eliminated.contains(candidate)) return;
                    if (evaluate_modulo(table.family, k_index, n_index, rule.prime) != 0U) {
                        throw std::logic_error("compiled rule does not divide matched candidate");
                    }
                    const auto value = evaluate_exact(table.family, k_index, n_index);
                    if (!proper_factor_witness(value, rule.prime)) return;
                    eliminated.emplace(
                        candidate, Elimination{candidate, rule.prime, value, rule.proof});
                });
            });
        }
    }
    std::vector<Elimination> result;
    result.reserve(eliminated.size());
    for (auto& [candidate, elimination] : eliminated) {
        static_cast<void>(candidate);
        result.push_back(std::move(elimination));
    }
    return result;
}

std::vector<Elimination> scalar_reference_eliminations(
    const AffineExponentialFamily& family,
    const std::vector<std::uint64_t>& primes) {
    validate_family(family);
    const auto checked_primes = normalized_primes(primes);
    std::vector<Elimination> result;
    for (std::uint64_t k_index = 0U; k_index < family.k.size(); ++k_index) {
        if (!parity_matches(family.k.value_at(k_index), family.k_parity)) continue;
        for (std::uint64_t n_index = 0U; n_index < family.n.size(); ++n_index) {
            if (!parity_matches(family.n.value_at(n_index), family.n_parity)) continue;
            for (const auto prime : checked_primes) {
                if (evaluate_modulo(family, k_index, n_index, prime) != 0U) continue;
                const auto value = evaluate_exact(family, k_index, n_index);
                if (!proper_factor_witness(value, prime)) continue;
                result.push_back({
                    {k_index, n_index}, prime, value,
                    "scalar direct evaluation reconstructed q=" + std::to_string(prime)});
                break;
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const Elimination& left, const Elimination& right) {
        return left.candidate < right.candidate;
    });
    return result;
}

std::uint64_t reconstruct_factor(const Elimination& elimination) {
    if (!proper_factor_witness(elimination.value, elimination.factor)) {
        throw std::invalid_argument("elimination does not contain a valid proper prime factor witness");
    }
    return elimination.factor;
}

}  // namespace primeforge::congruence
