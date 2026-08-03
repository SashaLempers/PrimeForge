// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/mvp/search_config.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::string_view valid_yaml =
    "schema: primeforge.search.v1\n"
    "campaign_name: known-proth-small\n"
    "family: proth\n"
    "expression: k*2^n+1\n"
    "parameters:\n"
    "  k:\n"
    "    start: 1\n"
    "    stop: 31\n"
    "    step: 2\n"
    "  n:\n"
    "    start: 5\n"
    "    stop: 14\n"
    "    step: 1\n"
    "constraints:\n"
    "  odd_k: true\n"
    "  k_less_than_2_pow_n: true\n"
    "work_units:\n"
    "  candidates_per_unit: 32\n"
    "sieve:\n"
    "  maximum_prime: 43\n"
    "checkpoint:\n"
    "  every_candidates: 16\n"
    "output:\n"
    "  directory: out/campaigns/known-proth-small\n"
    "engines:\n"
    "  pari_gp:\n"
    "    path: out/oracles/pari-gp64-2.17.4.exe\n"
    "    sha256: 518ea54d23832211356c99d1bb58b74a3f0acd354a965543e7bcca9b34030119\n"
    "  flint:\n"
    "    path: out/oracles/flint/flint-primality-oracle.exe\n"
    "    sha256: 5e62bcac0e324d14914979e4f565eab2080da0e215cfff5c97e3fb48368facd4\n";

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_failure(Function&& function, const std::string& message) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}

[[nodiscard]] std::string replace_once(
    std::string text, const std::string& from, const std::string& to) {
    const auto position = text.find(from);
    if (position == std::string::npos) throw std::logic_error("test replacement marker absent");
    text.replace(position, from.size(), to);
    return text;
}

}  // namespace

int main() {
    try {
        const primeforge::PortableSha256Provider sha256;
        const auto config = primeforge::mvp::parse_search_config(valid_yaml);
        const auto crlf = replace_once(std::string{valid_yaml}, "\n", "\r\n");
        const auto crlf_config = primeforge::mvp::parse_search_config(crlf);
        check(primeforge::mvp::canonical_search_config(config) ==
                  primeforge::mvp::canonical_search_config(crlf_config),
              "line endings do not change canonical configuration");
        check(primeforge::mvp::candidate_count(config) == 160U,
              "Cartesian campaign cardinality");
        const auto first = primeforge::mvp::candidate_at(config, 0U);
        const auto last = primeforge::mvp::candidate_at(config, 159U);
        check(first.k == 1U && first.n == 5U && first.value == 33U,
              "first candidate mapping");
        check(last.k == 31U && last.n == 14U && last.value == 507'905U,
              "last candidate mapping");
        const auto plan = primeforge::mvp::build_campaign_plan(config, sha256);
        check(plan.configuration_sha256 ==
                  "b72d1b3f2bfc5bdd3f3ee651d04735b20f0957c415b9afd412d40e3eb24591e0",
              "known campaign canonical SHA-256");
        const auto rendered = primeforge::mvp::render_search_config_yaml(config);
        const auto rendered_config = primeforge::mvp::parse_search_config(rendered);
        check(primeforge::mvp::canonical_search_config(rendered_config) ==
                  primeforge::mvp::canonical_search_config(config),
              "rendered recovery configuration round-trips logically");
        check(plan.candidate_count == 160U && plan.work_units.size() == 5U &&
                  plan.coverage.valid,
              "exact five-unit campaign plan");
        check(plan.work_units.front().interval.begin == 0U &&
                  plan.work_units.back().interval.end == 160U,
              "work-unit boundary ownership");
        check(primeforge::mvp::canonical_inspection(plan).find("\n") == std::string::npos,
              "canonical inspection is one stable line");
        check(primeforge::mvp::make_affine_family(config).base == 2,
              "MVP affine family is base two");

        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::parse_search_config(
                std::string{valid_yaml} + "campaign_name: duplicate\n")); },
            "duplicate key rejected");
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::parse_search_config(
                std::string{valid_yaml} + "unknown: value\n")); },
            "unknown key rejected");
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::parse_search_config(
                std::string{valid_yaml} + "unknown:\n")); },
            "unknown empty section rejected");
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::parse_search_config(
                replace_once(std::string{valid_yaml},
                             "parameters:\n", "parameters:\nparameters:\n"))); },
            "duplicate section rejected");
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::parse_search_config(
                replace_once(std::string{valid_yaml},
                             "known-proth-small", "\"unterminated\\\""))); },
            "escaped closing quote cannot hide an unterminated scalar");
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::parse_search_config(
                replace_once(std::string{valid_yaml}, "  k:\n", "\tk:\n"))); },
            "tab indentation rejected");
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::parse_search_config(
                replace_once(std::string{valid_yaml}, "start: 1", "start: 2"))); },
            "even k progression rejected");
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::parse_search_config(
                replace_once(std::string{valid_yaml}, "stop: 31", "stop: 255"))); },
            "family violating global k<2^n rejected");
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::parse_search_config(
                replace_once(std::string{valid_yaml},
                             "out/campaigns/known-proth-small", "../escape"))); },
            "path escape rejected");

        std::cout << "mvp_candidate_count=" << plan.candidate_count << '\n'
                  << "mvp_work_unit_count=" << plan.work_units.size() << '\n'
                  << "mvp_configuration_sha256=" << plan.configuration_sha256 << '\n'
                  << "PrimeForge MVP planning tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge MVP planning tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
