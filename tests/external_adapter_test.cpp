// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/engine/external_adapter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace engine = primeforge::engine;

class CountingSha256Provider final : public primeforge::Sha256Provider {
public:
    [[nodiscard]] primeforge::Sha256Digest digest(
        const std::span<const std::byte> bytes) const override {
        calls_.fetch_add(1U, std::memory_order_relaxed);
        bytes_.fetch_add(bytes.size(), std::memory_order_relaxed);
        return provider_.digest(bytes);
    }

    [[nodiscard]] std::uint64_t calls() const noexcept {
        return calls_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t bytes() const noexcept {
        return bytes_.load(std::memory_order_relaxed);
    }

private:
    primeforge::PortableSha256Provider provider_;
    mutable std::atomic_uint64_t calls_{};
    mutable std::atomic_uint64_t bytes_{};
};

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

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read test artifact");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::size_t count_batch_segment_directories(
    const std::filesystem::path& root) {
    std::size_t count{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
        if (entry.is_directory() &&
            entry.path().filename().string().starts_with("batch-segment-")) {
            ++count;
        }
    }
    return count;
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
        config.required_runtime_files = {{fixture, hash_file(fixture, sha256)}};
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

        CountingSha256Provider counting_sha256;
        engine::ExternalEngineAdapter cached_adapter{config, counting_sha256};
        const auto cached_first = cached_adapter.run(
            {"cached-first", "fixture", "FIXTURE:PRP", work_root});
        const auto installation_hash_calls = counting_sha256.calls();
        const auto installation_hash_bytes = counting_sha256.bytes();
        const auto cached_second = cached_adapter.run(
            {"cached-second", "fixture", "FIXTURE:COMPOSITE", work_root});
        check(cached_first.status.primality == primeforge::PrimalityStatus::probable_prime &&
                  cached_second.status.primality == primeforge::PrimalityStatus::composite,
              "cached installation validation preserves classifications");
        check(installation_hash_calls == 2U && counting_sha256.calls() == installation_hash_calls &&
                  installation_hash_bytes != 0U && counting_sha256.bytes() == installation_hash_bytes,
              "unchanged installation is hashed exactly once per adapter instance");

        auto flint_batch_config = config;
        flint_batch_config.kind = engine::ExternalEngineKind::flint;
        flint_batch_config.stable_id = "primeforge.fixture.flint-batch.v1";
        flint_batch_config.batch_parallel_processes = 4U;
        engine::ExternalEngineAdapter flint_batch{flint_batch_config, sha256};
        const std::array batch_requests{
            primeforge::EngineRequest{"batch-0", "fixture", "PROVEN_PRIME", work_root},
            primeforge::EngineRequest{"batch-1", "fixture", "COMPOSITE", work_root},
            primeforge::EngineRequest{"batch-2", "fixture", "PROVEN_PRIME", work_root}};
        const auto batch_results = flint_batch.run_batch(batch_requests);
        check(batch_results.size() == batch_requests.size() &&
                  batch_results[0].status.primality == primeforge::PrimalityStatus::proven_prime &&
                  batch_results[1].status.primality == primeforge::PrimalityStatus::composite &&
                  batch_results[2].status.primality == primeforge::PrimalityStatus::proven_prime,
              "FLINT batch output retains request ordering and exact classifications");
        check(flint_batch.recommended_parallelism() == 4U &&
                  std::filesystem::is_regular_file(batch_results[0].raw_stdout_path) &&
                  std::filesystem::is_regular_file(batch_results[1].raw_stdout_path),
              "FLINT batch retains deterministic per-request raw evidence");

        for (const auto invalid_process_count : {0U, 3U, 16U}) {
            auto invalid_config = flint_batch_config;
            invalid_config.batch_parallel_processes = invalid_process_count;
            expect_failure(
                [&] {
                    engine::ExternalEngineAdapter invalid_adapter{
                        invalid_config, sha256};
                    static_cast<void>(invalid_adapter);
                },
                "unsupported FLINT batch process count rejected");
        }
        auto non_flint_parallel_config = config;
        non_flint_parallel_config.stable_id =
            "primeforge.fixture.non-flint-parallelism.v1";
        non_flint_parallel_config.batch_parallel_processes = 8U;
        engine::ExternalEngineAdapter non_flint_parallel_adapter{
            non_flint_parallel_config, sha256};
        check(non_flint_parallel_adapter.recommended_parallelism() == 1U,
              "non-FLINT adapters never advertise fictitious batch parallelism");

        constexpr std::size_t barrier_request_count = 8U;
        const std::array<std::size_t, 4U> accepted_process_counts{1U, 2U, 4U, 8U};
        for (const auto process_count : accepted_process_counts) {
            auto parallel_config = flint_batch_config;
            parallel_config.stable_id =
                "primeforge.fixture.flint-parallel." + std::to_string(process_count);
            parallel_config.batch_parallel_processes = process_count;
            parallel_config.timeout = std::chrono::milliseconds{5'000};
            engine::ExternalEngineAdapter parallel_adapter{parallel_config, sha256};
            check(parallel_adapter.recommended_parallelism() == process_count,
                  "configured FLINT process count is reported exactly");

            const auto run_root =
                work_root / ("parallel-" + std::to_string(process_count));
            const auto barrier_root =
                work_root / ("barrier-" + std::to_string(process_count));
            const auto barrier_target = process_count;
            std::vector<primeforge::EngineRequest> parallel_requests;
            parallel_requests.reserve(barrier_request_count);
            for (std::size_t index = 0U; index < barrier_request_count; ++index) {
                const auto input = std::string{"BARRIER_PROVEN|"} +
                                   barrier_root.generic_string() + "|request-" +
                                   std::to_string(index) + "|" +
                                   std::to_string(barrier_target) + "|" +
                                   std::string(2'500U, 'x');
                parallel_requests.push_back(
                    {"parallel-" + std::to_string(process_count) + "-" +
                         std::to_string(index),
                     "fixture", input, run_root});
            }
            const auto parallel_results =
                parallel_adapter.run_batch(parallel_requests);
            check(parallel_results.size() == parallel_requests.size(),
                  "parallel FLINT batch preserves result cardinality");
            std::set<std::string> segment_stderr;
            for (std::size_t index = 0U; index < parallel_results.size(); ++index) {
                check(parallel_results[index].status.primality ==
                              primeforge::PrimalityStatus::proven_prime &&
                          parallel_results[index].engine_executable_sha256 ==
                              flint_batch_config.expected_executable_sha256,
                      "parallel FLINT results retain request order and identity");
                check(parallel_results[index].raw_stdout_path.parent_path().filename() ==
                          parallel_requests[index].job_id,
                      "ordered raw evidence remains attached to its request job id");
                const auto stderr_text = read_text(parallel_results[index].raw_stderr_path);
                check(stderr_text.find(
                          "FIXTURE_ARGUMENT_COUNT=" +
                          std::to_string(barrier_request_count / process_count)) !=
                          std::string::npos,
                      "target segment size follows configured process count");
                segment_stderr.insert(stderr_text);
            }
            check(segment_stderr.size() == process_count,
                  "configured process count creates the expected unique real segments");
            check(count_batch_segment_directories(run_root) == 0U,
                  "parallelism creates no persistent segment-only artifacts");
        }

        auto tiny_tail_config = flint_batch_config;
        tiny_tail_config.stable_id = "primeforge.fixture.flint-tiny-tail.v1";
        tiny_tail_config.batch_parallel_processes = 4U;
        engine::ExternalEngineAdapter tiny_tail_adapter{tiny_tail_config, sha256};
        const auto tiny_tail_root = work_root / "tiny-tail";
        const std::array<std::size_t, 5U> uneven_padding{
            5'700U, 5'800U, 5'600U, 5'900U, 1U};
        std::vector<primeforge::EngineRequest> tiny_tail_requests;
        tiny_tail_requests.reserve(uneven_padding.size());
        for (std::size_t index = 0U; index < uneven_padding.size(); ++index) {
            tiny_tail_requests.push_back(
                {"tiny-tail-" + std::to_string(index), "fixture",
                 "PADDED_PROVEN|" + std::to_string(index) + "|" +
                     std::string(uneven_padding[index], 't'),
                 tiny_tail_root});
        }
        const auto tiny_tail_results =
            tiny_tail_adapter.run_batch(tiny_tail_requests);
        std::set<std::string> tiny_tail_segments;
        for (const auto& tiny_tail_result : tiny_tail_results) {
            check(tiny_tail_result.status.primality ==
                      primeforge::PrimalityStatus::proven_prime,
                  "uneven partition preserves classifications");
            tiny_tail_segments.insert(read_text(tiny_tail_result.raw_stderr_path));
        }
        check(tiny_tail_segments.size() == 4U,
              "uneven inputs with a tiny tail produce exactly P feasible segments");
        check(std::ranges::count_if(
                  tiny_tail_segments, [](const std::string& stderr_text) {
                      return stderr_text.find("FIXTURE_ARGUMENT_COUNT=2") !=
                             std::string::npos;
                  }) == 1,
              "tiny tail is deterministically retained in the final balanced segment");
        check(count_batch_segment_directories(tiny_tail_root) == 0U,
              "balanced tiny-tail partition leaves no segment-only artifacts");

        auto failure_config = flint_batch_config;
        failure_config.stable_id = "primeforge.fixture.flint-fail-closed.v1";
        failure_config.batch_parallel_processes = 4U;
        engine::ExternalEngineAdapter failure_adapter{failure_config, sha256};
        const auto failure_root = work_root / "fail-closed";
        const std::string long_padding(12'000U, 'z');
        const std::array failure_requests{
            primeforge::EngineRequest{
                "failure-0", "fixture", "PADDED_PROVEN|" + long_padding, failure_root},
            primeforge::EngineRequest{
                "failure-1", "fixture", "FAIL_PROCESS|" + long_padding, failure_root},
            primeforge::EngineRequest{
                "failure-2", "fixture", "OMIT_OUTPUT|" + long_padding, failure_root},
            primeforge::EngineRequest{
                "failure-3", "fixture", "PADDED_COMPOSITE|" + long_padding,
                failure_root}};
        const auto failure_results = failure_adapter.run_batch(failure_requests);
        check(failure_results.size() == failure_requests.size() &&
                  failure_results[0].status.primality ==
                      primeforge::PrimalityStatus::proven_prime &&
                  failure_results[1].status.primality ==
                      primeforge::PrimalityStatus::untested &&
                  failure_results[1].diagnostics == "PROCESS_EXIT_NONZERO" &&
                  failure_results[2].status.primality ==
                      primeforge::PrimalityStatus::untested &&
                  failure_results[2].diagnostics == "BATCH_OUTPUT_COUNT_MISMATCH" &&
                  failure_results[3].status.primality ==
                      primeforge::PrimalityStatus::composite,
              "segment failures are fail-closed without reordering healthy results");

        auto oversized_config = flint_batch_config;
        oversized_config.stable_id = "primeforge.fixture.flint-oversized.v1";
        engine::ExternalEngineAdapter oversized_adapter{oversized_config, sha256};
        const std::array oversized_requests{
            primeforge::EngineRequest{
                "oversized-0", "fixture", std::string(23'998U, '1'),
                work_root / "oversized"},
            primeforge::EngineRequest{
                "oversized-1", "fixture", "PROVEN_PRIME", work_root / "oversized"}};
        expect_failure(
            [&] { static_cast<void>(oversized_adapter.run_batch(oversized_requests)); },
            "single argument exceeding the segment budget is rejected fail-closed");
        check(!std::filesystem::exists(work_root / "oversized"),
              "oversized batch preflight creates no work-directory artifacts");

        const auto invalid_job_root = work_root / "invalid-job-preflight";
        const std::array invalid_job_requests{
            primeforge::EngineRequest{
                "would-have-been-valid", "fixture", "PROVEN_PRIME", invalid_job_root},
            primeforge::EngineRequest{
                "../escape", "fixture", "COMPOSITE", invalid_job_root}};
        expect_failure(
            [&] { static_cast<void>(oversized_adapter.run_batch(invalid_job_requests)); },
            "all batch job ids are validated before preparing any request");
        check(!std::filesystem::exists(invalid_job_root),
              "invalid later job id leaves no partial batch artifacts");

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

        auto runtime_mismatch_config = config;
        runtime_mismatch_config.stable_id = "primeforge.fixture.runtime-hash-mismatch.v1";
        runtime_mismatch_config.required_runtime_files.front().expected_sha256 =
            std::string(64U, '0');
        engine::ExternalEngineAdapter runtime_mismatch_adapter{
            runtime_mismatch_config, sha256};
        const auto runtime_mismatch_result = runtime_mismatch_adapter.run(
            {"known-runtime-hash-mismatch", "fixture", "FIXTURE:COMPOSITE", work_root});
        check(runtime_mismatch_result.status.primality ==
                  primeforge::PrimalityStatus::untested &&
                  runtime_mismatch_result.diagnostics ==
                      "RUNTIME_FILE_PREFLIGHT_FAILED" &&
                  !std::filesystem::exists(work_root / "known-runtime-hash-mismatch"),
              "runtime dependency hash mismatch blocks process execution");

        expect_failure(
            [&] { static_cast<void>(adapter.prepare({"../escape", "fixture", "x", work_root})); },
            "path traversal job id rejected");
        std::cout << "external_parser_kinds_checked=9\n"
                  << "timeout_enforced=YES\n"
                  << "memory_limit_configured=YES\n"
                  << "raw_outputs_retained=YES\n"
                  << "installation_hash_cache=YES\n"
                  << "flint_batch_ordering=YES\n"
                  << "flint_batch_parallel_processes=1,2,4,8\n"
                  << "flint_batch_fail_closed=YES\n"
                  << "PrimeForge external adapter tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge external adapter tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
