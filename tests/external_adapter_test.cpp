// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/engine/external_adapter.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

namespace {

namespace engine = primeforge::engine;

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_failure(Function&& function, const std::string& message) {
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}

[[nodiscard]] std::string hash_file(
    const std::filesystem::path& path, const primeforge::Sha256Provider& sha256) {
    std::ifstream input{path, std::ios::binary};
    const std::string content{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return primeforge::sha256_to_hex(
        sha256.digest(std::as_bytes(std::span{content.data(), content.size()})));
}

void check_parser(
    const engine::ExternalEngineKind kind,
    const std::string_view output,
    const primeforge::PrimalityStatus status) {
    const auto result = engine::parse_external_output(
        kind, "primeforge-external-parser-v1", output, "");
    check(result.status.primality == status, "known parser vector");
    check(result.status.verification == primeforge::VerificationStatus::unverified,
          "external text never self-verifies");
    check(result.status.novelty == primeforge::NoveltyStatus::not_checked,
          "external text never checks novelty");
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 3) throw std::invalid_argument("fixture executable and work root required");
        const std::filesystem::path fixture = std::filesystem::absolute(argv[1]);
        const std::filesystem::path work_root = std::filesystem::absolute(argv[2]);
        std::filesystem::remove_all(work_root);
        std::filesystem::create_directories(work_root);
        const primeforge::PortableSha256Provider sha256;

        check_parser(engine::ExternalEngineKind::primesieve, "168\n",
                     primeforge::PrimalityStatus::untested);
        check_parser(engine::ExternalEngineKind::flint, "PROVEN_PRIME\n",
                     primeforge::PrimalityStatus::proven_prime);
        check_parser(engine::ExternalEngineKind::flint, "COMPOSITE\n",
                     primeforge::PrimalityStatus::composite);
        check_parser(engine::ExternalEngineKind::pari_gp, "PRIMEFORGE:PROVEN_PRIME\n",
                     primeforge::PrimalityStatus::proven_prime);
        check_parser(engine::ExternalEngineKind::pari_gp_certificate,
                     "PRIMEFORGE:CERTIFICATE_VALID\n",
                     primeforge::PrimalityStatus::proven_prime);
        check_parser(engine::ExternalEngineKind::openpfgw, "PRIMEFORGE_PFGW:PRP\n",
                     primeforge::PrimalityStatus::probable_prime);
        check_parser(engine::ExternalEngineKind::genefer22, "PRIMEFORGE_GENEFER:PRP\n",
                     primeforge::PrimalityStatus::probable_prime);
        check_parser(engine::ExternalEngineKind::mersenne_prpll, "PRIMEFORGE_PRPLL:PRP\n",
                     primeforge::PrimalityStatus::probable_prime);
        check_parser(engine::ExternalEngineKind::mlucas, "PRIMEFORGE_MLUCAS:COMPOSITE\n",
                     primeforge::PrimalityStatus::composite);
        check_parser(engine::ExternalEngineKind::gmp_ecm, "PRIMEFORGE_ECM:FACTOR=7\n",
                     primeforge::PrimalityStatus::composite);
        check_parser(engine::ExternalEngineKind::gmp_ecm, "PRIMEFORGE_ECM:NO_FACTOR\n",
                     primeforge::PrimalityStatus::untested);

        for (const auto kind : {
                 engine::ExternalEngineKind::openpfgw,
                 engine::ExternalEngineKind::genefer22,
                 engine::ExternalEngineKind::mersenne_prpll,
                 engine::ExternalEngineKind::mlucas}) {
            const auto mutated = engine::parse_external_output(
                kind, "primeforge-external-parser-v1", "This number is prime!\n", "");
            check(mutated.status.primality == primeforge::PrimalityStatus::untested &&
                      mutated.diagnostics == "UNKNOWN_OUTPUT_FORMAT",
                  "unrecognized format is UNKNOWN/UNTESTED");
        }
        expect_failure(
            [&] {
                static_cast<void>(engine::parse_external_output(
                    engine::ExternalEngineKind::flint, "v2", "PROVEN_PRIME\n", ""));
            },
            "unknown parser version rejected clearly");

        engine::ExternalAdapterConfig config;
        config.kind = engine::ExternalEngineKind::fixture;
        config.stable_id = "primeforge.fixture.external.v1";
        config.parser_version = "primeforge-external-parser-v1";
        config.executable = fixture;
        config.expected_executable_sha256 = hash_file(fixture, sha256);
        config.supported_families = {"fixture"};
        config.timeout = std::chrono::milliseconds{2'000};
        config.memory_limit_bytes = 128U * 1024U * 1024U;
        engine::ExternalEngineAdapter adapter{config, sha256};
        check(adapter.estimate_cost(1U).basis == "UNKNOWN", "unbenchmarked cost remains unknown");
        const primeforge::EngineRequest request{
            "known-prp", "fixture", "FIXTURE:PRP", work_root};
        const auto result = adapter.run(request);
        check(result.status.primality == primeforge::PrimalityStatus::probable_prime,
              "isolated fixture process parsed");
        check(std::filesystem::is_regular_file(result.raw_stdout_path) &&
                  std::filesystem::is_regular_file(result.raw_stderr_path),
              "raw process output retained");

        auto timeout_config = config;
        timeout_config.stable_id = "primeforge.fixture.timeout.v1";
        timeout_config.timeout = std::chrono::milliseconds{20};
        engine::ExternalEngineAdapter timeout_adapter{timeout_config, sha256};
        const auto timeout_result = timeout_adapter.run(
            {"known-timeout", "fixture", "SLEEP", work_root});
        check(timeout_result.status.primality == primeforge::PrimalityStatus::untested &&
                  timeout_result.diagnostics == "PROCESS_TIMEOUT",
              "timeout terminates isolated job and returns untested");

        auto mismatch_config = config;
        mismatch_config.stable_id = "primeforge.fixture.hash-mismatch.v1";
        mismatch_config.expected_executable_sha256 = std::string(64U, '0');
        engine::ExternalEngineAdapter mismatch_adapter{mismatch_config, sha256};
        const auto mismatch_result = mismatch_adapter.run(
            {"known-hash-mismatch", "fixture", "FIXTURE:COMPOSITE", work_root});
        check(mismatch_result.status.primality == primeforge::PrimalityStatus::untested &&
                  mismatch_result.diagnostics == "EXECUTABLE_PREFLIGHT_FAILED" &&
                  !std::filesystem::exists(work_root / "known-hash-mismatch"),
              "binary hash mismatch blocks process execution and classification");

        expect_failure(
            [&] { static_cast<void>(adapter.prepare({"../escape", "fixture", "x", work_root})); },
            "path traversal job id rejected");
        std::cout << "external_parser_kinds_checked=9\n"
                  << "timeout_enforced=YES\n"
                  << "memory_limit_configured=YES\n"
                  << "raw_outputs_retained=YES\n"
                  << "PrimeForge external adapter tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge external adapter tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
