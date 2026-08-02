// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/core/system_info.hpp"
#include "primeforge/mvp/search_config.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string hash_file(
    const std::filesystem::path& path, const primeforge::Sha256Provider& sha256) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read engine executable: " + path.string());
    const std::string content{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return primeforge::sha256_to_hex(
        sha256.digest(std::as_bytes(std::span{content.data(), content.size()})));
}

void print_engine(
    const std::string_view name,
    const primeforge::mvp::EngineExecutable& engine,
    const primeforge::Sha256Provider& sha256) {
    const auto path = std::filesystem::absolute(engine.path);
    std::cout << "engine." << name << ".path=" << path.string() << '\n';
    if (!std::filesystem::is_regular_file(path)) {
        std::cout << "engine." << name << ".availability=UNAVAILABLE\n";
        return;
    }
    const auto observed = hash_file(path, sha256);
    std::cout << "engine." << name << ".observed_sha256=" << observed << '\n'
              << "engine." << name << ".availability="
              << (observed == engine.expected_sha256 ? "VERIFIED" : "HASH_MISMATCH") << '\n';
    if (observed != engine.expected_sha256) {
        throw std::runtime_error(std::string{name} + " executable hash mismatch");
    }
}

[[nodiscard]] std::filesystem::path config_argument(const int argc, char** argv) {
    if (argc != 4 || std::string_view{argv[2]} != "--config") {
        throw std::invalid_argument("usage: primeforge inspect --config search.yaml");
    }
    return argv[3];
}

void run_selftest() {
    const primeforge::PortableSha256Provider sha256;
    constexpr std::string_view minimal =
        "schema: primeforge.search.v1\n"
        "campaign_name: selftest\n"
        "family: proth\n"
        "expression: k*2^n+1\n"
        "parameters:\n"
        "  k:\n"
        "    start: 1\n"
        "    stop: 3\n"
        "    step: 2\n"
        "  n:\n"
        "    start: 2\n"
        "    stop: 3\n"
        "    step: 1\n"
        "constraints:\n"
        "  odd_k: true\n"
        "  k_less_than_2_pow_n: true\n"
        "work_units:\n"
        "  candidates_per_unit: 2\n"
        "sieve:\n"
        "  maximum_prime: 43\n"
        "checkpoint:\n"
        "  every_candidates: 1\n"
        "output:\n"
        "  directory: out/selftest\n"
        "engines:\n"
        "  pari_gp:\n"
        "    path: out/oracles/pari.exe\n"
        "    sha256: 0000000000000000000000000000000000000000000000000000000000000000\n"
        "  flint:\n"
        "    path: out/oracles/flint.exe\n"
        "    sha256: 0000000000000000000000000000000000000000000000000000000000000000\n";
    const auto config = primeforge::mvp::parse_search_config(minimal);
    const auto plan = primeforge::mvp::build_campaign_plan(config, sha256);
    if (!plan.coverage.valid || plan.candidate_count != 4U) {
        throw std::logic_error("MVP planning self-test failed");
    }
    std::cout << primeforge::format_system_info(primeforge::collect_system_info())
              << "mvp.plan_candidates=" << plan.candidate_count << '\n'
              << "mvp.status=PASS\n";
}

void run_inspect(const std::filesystem::path& config_path) {
    const primeforge::PortableSha256Provider sha256;
    const auto config = primeforge::mvp::load_search_config(config_path);
    const auto plan = primeforge::mvp::build_campaign_plan(config, sha256);
    std::cout << "campaign.name=" << config.campaign_name << '\n'
              << "campaign.id=" << plan.campaign_id << '\n'
              << "campaign.candidates=" << plan.candidate_count << '\n'
              << "campaign.work_units=" << plan.work_units.size() << '\n'
              << "campaign.coverage=EXACT\n";
    print_engine("pari_gp", config.pari_gp, sha256);
    print_engine("flint", config.flint, sha256);
    std::cout << "inspection.json=" << primeforge::mvp::canonical_inspection(plan) << '\n'
              << "inspect.status=PASS\n";
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc < 2) {
            throw std::invalid_argument(
                "usage: primeforge <selftest|inspect|search|resume|verify> [options]");
        }
        const std::string_view command{argv[1]};
        if (command == "selftest" && argc == 2) {
            run_selftest();
        } else if (command == "inspect") {
            run_inspect(config_argument(argc, argv));
        } else if (command == "search" || command == "resume" || command == "verify") {
            throw std::invalid_argument(std::string{command} + " is scheduled for MVP-02/MVP-03");
        } else {
            throw std::invalid_argument("unknown or malformed primeforge command");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge: " << error.what() << '\n';
        return 1;
    }
}
