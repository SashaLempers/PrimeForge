// SPDX-License-Identifier: Apache-2.0

#include "primeforge/congruence/compiler.hpp"
#include "primeforge/core/sha256.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

template <typename Integer>
[[nodiscard]] Integer parse_integer(const std::string_view text, const std::string_view label) {
    Integer value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid " + std::string{label} + ": " + std::string{text});
    }
    return value;
}

[[nodiscard]] primeforge::congruence::ParityConstraint parse_parity(
    const std::string_view text) {
    if (text == "any") return primeforge::congruence::ParityConstraint::any;
    if (text == "even") return primeforge::congruence::ParityConstraint::even;
    if (text == "odd") return primeforge::congruence::ParityConstraint::odd;
    throw std::invalid_argument("parity must be any, even, or odd");
}

void print_usage() {
    std::cout
        << "Usage: primeforge-congruence --base B --constant C "
           "--k-min V --k-max V --k-step S --n-min V --n-max V --n-step S "
           "--prime Q [--prime Q ...] [--k-parity any|even|odd] "
           "[--n-parity any|even|odd] [--period-multiplier M] [--canonical]\n";
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        primeforge::congruence::AffineExponentialFamily family;
        family.k.step = 0U;
        family.n.step = 0U;
        primeforge::congruence::CompileOptions options;
        std::vector<std::uint64_t> primes;
        bool have_base = false;
        bool have_constant = false;
        bool have_k_minimum = false;
        bool have_k_maximum = false;
        bool have_n_minimum = false;
        bool have_n_maximum = false;
        bool canonical = false;

        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index]};
            const auto next = [&]() -> std::string_view {
                if (index + 1 >= argc) {
                    throw std::invalid_argument("missing value after " + std::string{argument});
                }
                return argv[++index];
            };
            if (argument == "--base") {
                family.base = parse_integer<std::int64_t>(next(), "base");
                have_base = true;
            } else if (argument == "--constant") {
                family.constant = parse_integer<std::int64_t>(next(), "constant");
                have_constant = true;
            } else if (argument == "--k-min") {
                family.k.minimum = parse_integer<std::int64_t>(next(), "k minimum");
                have_k_minimum = true;
            } else if (argument == "--k-max") {
                family.k.maximum = parse_integer<std::int64_t>(next(), "k maximum");
                have_k_maximum = true;
            } else if (argument == "--k-step") {
                family.k.step = parse_integer<std::uint64_t>(next(), "k step");
            } else if (argument == "--n-min") {
                family.n.minimum = parse_integer<std::int64_t>(next(), "n minimum");
                have_n_minimum = true;
            } else if (argument == "--n-max") {
                family.n.maximum = parse_integer<std::int64_t>(next(), "n maximum");
                have_n_maximum = true;
            } else if (argument == "--n-step") {
                family.n.step = parse_integer<std::uint64_t>(next(), "n step");
            } else if (argument == "--prime") {
                primes.push_back(parse_integer<std::uint64_t>(next(), "prime"));
            } else if (argument == "--k-parity") {
                family.k_parity = parse_parity(next());
            } else if (argument == "--n-parity") {
                family.n_parity = parse_parity(next());
            } else if (argument == "--period-multiplier") {
                options.period_multiplier =
                    parse_integer<std::uint64_t>(next(), "period multiplier");
            } else if (argument == "--canonical") {
                canonical = true;
            } else if (argument == "--help") {
                print_usage();
                return 0;
            } else {
                throw std::invalid_argument("unknown argument: " + std::string{argument});
            }
        }
        if (!have_base || !have_constant || !have_k_minimum || !have_k_maximum ||
            !have_n_minimum || !have_n_maximum || family.k.step == 0U ||
            family.n.step == 0U || primes.empty()) {
            print_usage();
            return 2;
        }

        const primeforge::PortableSha256Provider sha256;
        const auto table = primeforge::congruence::compile_congruences(
            family, primes, options, sha256);
        const auto validation = primeforge::congruence::validate_compiled_table(table, sha256);
        std::uint64_t rule_count = 0U;
        std::cout << "table.format=" << table.format_version << '\n'
                  << "table.sha256=" << table.table_sha256 << '\n'
                  << "table.valid=" << (validation.valid ? "true" : "false") << '\n';
        for (const auto& prime_table : table.prime_tables) {
            rule_count += static_cast<std::uint64_t>(prime_table.rules.size());
            std::cout << "period.q=" << prime_table.period.prime
                      << ",order=" << prime_table.period.multiplicative_order
                      << ",selected=" << prime_table.period.selected_period
                      << ",n_index_period=" << prime_table.period.exponent_index_period
                      << ",rules=" << prime_table.rules.size() << '\n';
            for (const auto& rule : prime_table.rules) {
                std::cout << "proof=" << rule.proof << '\n';
            }
        }
        const auto eliminated = primeforge::congruence::apply_compiled_table(table, sha256);
        std::cout << "table.rules=" << rule_count << '\n'
                  << "table.eliminations=" << eliminated.size() << '\n';
        if (canonical) {
            std::cout << "table.canonical="
                      << primeforge::congruence::canonical_compiled_table_without_hash(table)
                      << '\n';
        }
        return validation.valid ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-congruence: " << error.what() << '\n';
        return 1;
    }
}
