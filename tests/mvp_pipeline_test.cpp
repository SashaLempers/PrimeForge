// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/engine/engine_adapter.hpp"
#include "primeforge/mvp/campaign_verifier.hpp"
#include "primeforge/mvp/flint_evidence_log.hpp"
#include "primeforge/mvp/search_config.hpp"
#include "primeforge/mvp/search_pipeline.hpp"
#include "primeforge/proth/proth.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct ExpectedPrime {
    std::uint64_t flat_index{};
    std::uint64_t k{};
    std::uint64_t n{};
    std::uint64_t value{};
};

[[nodiscard]] std::vector<ExpectedPrime> load_expected(
    const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) throw std::runtime_error("cannot open known MVP result corpus");
    std::string line;
    std::getline(input, line);
    if (line != "schema\tflat_index\tk\tn\tvalue\texpected_primality_status") {
        throw std::runtime_error("unexpected MVP result corpus header");
    }
    std::vector<ExpectedPrime> result;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::istringstream row{line};
        std::string schema;
        std::string index;
        std::string k;
        std::string n;
        std::string value;
        std::string status;
        std::getline(row, schema, '\t');
        std::getline(row, index, '\t');
        std::getline(row, k, '\t');
        std::getline(row, n, '\t');
        std::getline(row, value, '\t');
        std::getline(row, status, '\t');
        if (schema != "primeforge.mvp.expected-primes.v1" ||
            status != "PROVEN_PRIME" || row.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("invalid MVP result corpus row");
        }
        result.push_back({std::stoull(index), std::stoull(k), std::stoull(n),
                          std::stoull(value)});
    }
    return result;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read test output");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string read_native_certificate(
    const primeforge::mvp::NativeProofEvidence& evidence) {
    const auto bytes = read_file(evidence.artifact_path);
    if (!evidence.artifact_offset.has_value() || !evidence.artifact_length.has_value()) {
        return bytes;
    }
    const auto offset = static_cast<std::size_t>(*evidence.artifact_offset);
    const auto length = static_cast<std::size_t>(*evidence.artifact_length);
    if (offset > bytes.size() || length > bytes.size() - offset) {
        throw std::runtime_error("native certificate test slice is out of range");
    }
    return bytes.substr(offset, length);
}

void write_file(const std::filesystem::path& path, const std::string_view bytes) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("cannot write test output");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot complete test output write");
}

[[nodiscard]] std::string hash_text(
    const std::string_view bytes, const primeforge::Sha256Provider& sha256) {
    return primeforge::sha256_to_hex(
        sha256.digest(std::as_bytes(std::span{bytes.data(), bytes.size()})));
}

[[nodiscard]] std::uint64_t checkpoint_decimal(
    const std::string_view payload, const std::string_view field) {
    const auto marker = std::string{field} + "=";
    const auto marker_position = payload.find(marker);
    if (marker_position == std::string_view::npos) {
        throw std::runtime_error("checkpoint test field is absent");
    }
    const auto begin = marker_position + marker.size();
    const auto end = payload.find(';', begin);
    if (end == std::string_view::npos || end == begin) {
        throw std::runtime_error("checkpoint test field is malformed");
    }
    std::uint64_t result{};
    const auto converted = std::from_chars(
        payload.data() + begin, payload.data() + end, result);
    if (converted.ec != std::errc{} || converted.ptr != payload.data() + end) {
        throw std::runtime_error("checkpoint test field is not decimal");
    }
    return result;
}

[[nodiscard]] std::map<std::string, std::string> snapshot_regular_files(
    const std::filesystem::path& directory) {
    std::map<std::string, std::string> snapshot;
    if (!std::filesystem::is_directory(directory)) return snapshot;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto relative = entry.path().lexically_relative(directory).generic_string();
        snapshot.emplace(relative, read_file(entry.path()));
    }
    return snapshot;
}

[[nodiscard]] bool contains_atomic_temporary_file(
    const std::filesystem::path& directory) {
    if (!std::filesystem::is_directory(directory)) return false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".new") return true;
    }
    return false;
}

class KnownEngine final : public primeforge::EngineAdapter {
public:
    KnownEngine(
        std::string id,
        const std::set<std::uint64_t>& known_primes,
        const bool proof,
        const bool disagree = false,
        const bool mixed_raw_output = false)
        : id_{std::move(id)}, known_primes_{&known_primes},
          proof_{proof}, disagree_{disagree}, mixed_raw_output_{mixed_raw_output} {}

    [[nodiscard]] std::string_view id() const noexcept override { return id_; }

    [[nodiscard]] primeforge::EngineCapabilities capabilities() const override {
        return {{"primeforge.proth.uint64.v1"}, false, proof_, false};
    }

    [[nodiscard]] bool supports(
        const primeforge::EngineRequest& request) const noexcept override {
        return request.family_id == "primeforge.proth.uint64.v1";
    }

    [[nodiscard]] std::size_t recommended_parallelism() const noexcept override {
        return 1U;
    }

    [[nodiscard]] primeforge::EngineResult run(
        const primeforge::EngineRequest& request) override {
        ++run_calls_;
        return make_result(request, false);
    }

    [[nodiscard]] std::vector<primeforge::EngineResult> run_batch(
        const std::span<const primeforge::EngineRequest> requests) override {
        ++run_batch_calls_;
        last_batch_size_ = requests.size();
        std::vector<primeforge::EngineResult> results;
        results.reserve(requests.size());
        for (const auto& request : requests) {
            results.push_back(make_result(request, !proof_));
        }
        return results;
    }

    void reset_call_counts() noexcept {
        run_calls_ = 0U;
        run_batch_calls_ = 0U;
        last_batch_size_ = 0U;
    }

    [[nodiscard]] std::size_t run_calls() const noexcept { return run_calls_; }
    [[nodiscard]] std::size_t run_batch_calls() const noexcept { return run_batch_calls_; }
    [[nodiscard]] std::size_t last_batch_size() const noexcept { return last_batch_size_; }

private:
    [[nodiscard]] primeforge::EngineResult make_result(
        const primeforge::EngineRequest& request, const bool inline_raw_output) const {
        const auto value = std::stoull(request.canonical_input);
        bool prime = known_primes_->contains(value);
        if (disagree_ && value == *known_primes_->begin()) prime = !prime;
        const auto stdout_bytes =
            proof_ ? (prime ? std::string{"PRIMEFORGE:PROVEN_PRIME\n"}
                            : std::string{"PRIMEFORGE:COMPOSITE\n"})
                   : (prime ? std::string{"PROVEN_PRIME\n"}
                            : std::string{"COMPOSITE\n"});

        primeforge::EngineResult result;
        result.status.primality = prime ? primeforge::PrimalityStatus::proven_prime
                                        : primeforge::PrimalityStatus::composite;
        result.status.verification = primeforge::VerificationStatus::unverified;
        result.status.novelty = primeforge::NoveltyStatus::not_checked;
        result.diagnostics = prime ? "KNOWN_PROOF_FIXTURE" : "KNOWN_COMPOSITE_FIXTURE";
        result.engine_executable_sha256 = std::string(64U, proof_ ? 'a' : 'b');
        if (inline_raw_output) {
            result.raw_stdout_bytes = stdout_bytes;
            result.raw_stderr_bytes = std::string{};
            if (mixed_raw_output_) {
                const auto directory = request.working_directory / request.job_id;
                std::filesystem::create_directories(directory);
                result.raw_stdout_path = directory / "stdout.txt";
                result.raw_stderr_path = directory / "stderr.txt";
                write_file(result.raw_stdout_path, stdout_bytes);
                write_file(result.raw_stderr_path, "");
            }
        } else {
            const auto directory = request.working_directory / request.job_id;
            std::filesystem::create_directories(directory);
            result.raw_stdout_path = directory / "stdout.txt";
            result.raw_stderr_path = directory / "stderr.txt";
            write_file(result.raw_stdout_path, stdout_bytes);
            write_file(result.raw_stderr_path, "");
        }
        if (proof_ && prime) {
            const auto directory = request.working_directory / request.job_id;
            const auto certificate = directory / "certificate.txt";
            std::ofstream output{certificate, std::ios::binary};
            output << "KNOWN-CERTIFICATE:" << value << '\n';
            output.close();
            result.proof_artifact_paths.push_back(certificate);
        }
        return result;
    }

    std::string id_;
    const std::set<std::uint64_t>* known_primes_{};
    bool proof_{};
    bool disagree_{};
    bool mixed_raw_output_{};
    std::size_t run_calls_{};
    std::size_t run_batch_calls_{};
    std::size_t last_batch_size_{};
};

class KnownCertificateVerifier final : public primeforge::EngineAdapter {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "known-certificate-verifier";
    }

    [[nodiscard]] primeforge::EngineCapabilities capabilities() const override {
        return {{"primeforge.proth.uint64.v1"}, false, false, false};
    }

    [[nodiscard]] bool supports(
        const primeforge::EngineRequest& request) const noexcept override {
        return request.family_id == "primeforge.proth.uint64.v1";
    }

    [[nodiscard]] primeforge::EngineResult run(
        const primeforge::EngineRequest& request) override {
        const auto separator = request.canonical_input.find('|');
        if (separator == std::string::npos) {
            throw std::invalid_argument("known certificate request malformed");
        }
        const auto decimal = request.canonical_input.substr(0U, separator);
        const auto path = request.canonical_input.substr(separator + 1U);
        primeforge::EngineResult result;
        result.status.primality =
            read_file(path) == "KNOWN-CERTIFICATE:" + decimal + "\n"
                ? primeforge::PrimalityStatus::proven_prime
                : primeforge::PrimalityStatus::untested;
        result.status.verification = primeforge::VerificationStatus::unverified;
        result.status.novelty = primeforge::NoveltyStatus::not_checked;
        result.engine_executable_sha256 = std::string(64U, 'c');
        return result;
    }
};

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

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::invalid_argument("search configuration and result corpus paths required");
        }
        const primeforge::PortableSha256Provider sha256;
        auto config = primeforge::mvp::load_search_config(argv[1]);
        const auto expected = load_expected(argv[2]);
        std::set<std::uint64_t> known_primes;
        for (const auto& item : expected) {
            const auto candidate = primeforge::mvp::candidate_at(config, item.flat_index);
            check(candidate.k == item.k && candidate.n == item.n &&
                      candidate.value == item.value,
                  "expected-prime coordinates match the canonical campaign");
            check(known_primes.emplace(item.value).second,
                  "known prime values are unique");
        }
        check(expected.size() == 34U, "known corpus contains 34 primes");
        check(primeforge::mvp::select_sieve_threads(160U, 32U) == 1U &&
                  primeforge::mvp::select_sieve_threads(4'096U, 32U) == 4U &&
                  primeforge::mvp::select_sieve_threads(16'384U, 32U) == 16U &&
                  primeforge::mvp::select_sieve_threads(262'144U, 8U) == 8U &&
                  primeforge::mvp::select_sieve_threads(262'144U, 0U) == 1U,
              "sieve thread selection is bounded by work and available CPU threads");
        config.pari_gp.expected_sha256 = std::string(64U, 'a');
        config.flint.expected_sha256 = std::string(64U, 'b');
        config.output_directory = "mvp-pipeline-test-output";
        std::filesystem::remove_all(config.output_directory);

        KnownEngine proof{"known-pari-proof", known_primes, true};
        KnownEngine independent{"known-flint-independent", known_primes, false};
        for (const auto invalid_worker_count : {std::size_t{0U}, std::size_t{65U}}) {
            primeforge::mvp::SearchExecutionOptions invalid_options;
            invalid_options.native_proof_workers = invalid_worker_count;
            expect_failure(
                [&] {
                    static_cast<void>(primeforge::mvp::execute_search(
                        config, sha256, proof, independent, invalid_options));
                },
                "native proof worker count is bounded at the API boundary");
        }
        auto batched_prp = primeforge::prp::make_cpu_base2_strong_prp_batch_backend(
            17U, 2U);
        primeforge::mvp::SearchExecutionOptions batched_options;
        batched_options.prp_backend = batched_prp.get();
        batched_options.prp_batch_candidates = 17U;
        batched_options.native_proof_workers = 1U;
        const auto first = primeforge::mvp::execute_search(
            config, sha256, proof, independent, batched_options);
        check(first.completed && first.records.size() == 160U &&
                  first.plan.coverage.valid &&
                  std::filesystem::is_regular_file(first.coverage_report_path) &&
                  std::filesystem::is_regular_file(first.manifest_path) &&
                  std::filesystem::is_regular_file(first.timeline_json_path) &&
                  std::filesystem::is_regular_file(first.timeline_svg_path) &&
                  !first.timeline_events.empty(),
              "complete exact campaign output");
        check(independent.recommended_parallelism() == 1U &&
                  independent.run_batch_calls() > 0U && independent.run_calls() == 0U,
              "independent verification uses run_batch even at recommended parallelism one");
        check(first.proven_prime_count == known_primes.size() &&
                  first.composite_count == 160U - known_primes.size(),
              "known classification totals");
        check(first.prp_tested_count > 0U && first.prp_submitted_batches > 1U &&
                  first.prp_backend_id == batched_prp->id(),
              "MVP uses the bounded batched PRP backend");
        check(first.metrics.schema_version == 2U && first.metrics.total_ns > 0U &&
                  first.metrics.generation_ns > 0U && first.metrics.congruence_ns > 0U &&
                  first.metrics.sieve_ns > 0U && first.metrics.packing_ns > 0U &&
                  first.metrics.prp_cpu_ns > 0U && first.metrics.proof_ns > 0U &&
                  first.metrics.verification_ns > 0U && first.metrics.io_ns > 0U &&
                  first.metrics.checkpoint_ns > 0U &&
                  first.metrics.result_processing_ns > 0U &&
                  first.metrics.host_to_device_ns == 0U &&
                  first.metrics.kernel_ns == 0U && first.metrics.device_to_host_ns == 0U,
              "MVP reports complete integer stage metrics for the CPU baseline");
        check(first.externally_classified_count >= known_primes.size(),
              "every known prime reaches both external contracts");
        check(std::filesystem::is_regular_file(first.flint_evidence_path),
              "campaign owns one FLINT evidence journal");
        primeforge::mvp::FlintEvidenceLog first_flint_log{
            first.flint_evidence_path, sha256};
        std::vector<primeforge::mvp::FlintEvidenceSlice> first_flint_slices;
        std::uint64_t expected_flint_offset = 0U;
        std::uint64_t native_certificates = 0U;
        for (const auto& record : first.records) {
            const bool expected_prime =
                known_primes.contains(record.candidate.value);
            check(record.status.novelty == primeforge::NoveltyStatus::not_checked,
                  "novelty remains independent and unchecked");
            if (expected_prime) {
                check(record.status.primality == primeforge::PrimalityStatus::proven_prime &&
                          record.status.verification ==
                              primeforge::VerificationStatus::independently_verified &&
                          record.classification_method == "PROTH_CERTIFICATE_VALIDATED" &&
                          record.native_proth_certificate.has_value() &&
                          !record.primary_engine.has_value() &&
                          record.independent_engine.has_value(),
                      "prime has native proof artifact and independent agreement");
                const auto certificate_bytes =
                    read_native_certificate(*record.native_proth_certificate);
                const auto certificate =
                    primeforge::proth::parse_canonical_certificate(certificate_bytes);
                check(certificate.has_value() &&
                          certificate->k == record.candidate.k &&
                          certificate->n == record.candidate.n &&
                          certificate->value == record.candidate.value,
                      "stored native certificate binds exact candidate coordinates");
                ++native_certificates;
            } else {
                check(record.status.primality == primeforge::PrimalityStatus::composite,
                      "known composite remains composite");
            }
            check(record.status.primality != primeforge::PrimalityStatus::probable_prime,
                  "completed search never presents a PRP as proven by implication");
            if (record.independent_engine.has_value()) {
                const auto& evidence = *record.independent_engine;
                check(evidence.raw_log_path == first.flint_evidence_path &&
                          evidence.raw_log_offset.has_value() &&
                          evidence.raw_log_length.has_value() &&
                          *evidence.raw_log_offset == expected_flint_offset &&
                          *evidence.raw_log_length > 0U &&
                          evidence.raw_stdout_path.empty() &&
                          evidence.raw_stderr_path.empty(),
                      "independent evidence uses one exact contiguous FLINT journal slice");
                const primeforge::mvp::FlintEvidenceSlice slice{
                    *evidence.raw_log_offset, *evidence.raw_log_length};
                const auto stored = first_flint_log.read(slice);
                check(stored.job_id == "flint-" +
                                           std::to_string(record.candidate.flat_index) &&
                          stored.input == std::to_string(record.candidate.value),
                      "FLINT journal slice binds exact candidate coordinates");
                const auto canonical = primeforge::mvp::canonical_search_record(
                    record, first.output_directory);
                const auto exact_reference =
                    "\"raw_log_length\":\"" + std::to_string(slice.length) +
                    "\",\"raw_log_offset\":\"" + std::to_string(slice.offset) +
                    "\",\"raw_log_path\":\"external/flint/evidence.jsonl\","
                    "\"raw_stderr_path\":null,\"raw_stdout_path\":null";
                check(canonical.find(exact_reference) != std::string::npos,
                      "canonical result stores exact flat raw_log reference fields");
                first_flint_slices.push_back(slice);
                expected_flint_offset += slice.length;
            }
        }
        check(native_certificates == known_primes.size(),
              "every known prime uses the native Proth proof boundary");
        const auto complete_flint_prefix =
            first_flint_log.validate_complete_log(first_flint_slices);
        check(first_flint_slices.size() == first.externally_classified_count &&
                  complete_flint_prefix.size == expected_flint_offset,
              "FLINT journal has exactly one indexed record per external classification");
        const auto first_flint_inventory = snapshot_regular_files(
            first.output_directory / "external" / "flint");
        check(first_flint_inventory.size() == 1U &&
                  first_flint_inventory.contains("evidence.jsonl"),
              "completed campaign leaves no per-candidate FLINT files");
        const auto first_bytes = read_file(first.results_path);
        const auto first_flint_bytes = read_file(first.flint_evidence_path);
        const auto first_manifest = read_file(first.manifest_path);
        const auto first_certificates =
            snapshot_regular_files(first.output_directory / "proofs" / "proth");
        check(!first_certificates.empty() && first_certificates.size() <= 10U &&
                  first_certificates.size() < native_certificates &&
                  std::ranges::all_of(first_certificates, [](const auto& entry) {
                      return entry.first.starts_with("segment-") &&
                             entry.first.ends_with(".jsonl");
                  }),
              "native certificates share one deterministic journal per checkpoint segment");
        check(std::ranges::count(first_bytes, '\n') == 160,
              "one canonical JSONL record per candidate");
        KnownCertificateVerifier certificate_verifier;
        independent.reset_call_counts();
        const auto verified = primeforge::mvp::verify_campaign(
            first.results_path, sha256, certificate_verifier, independent);
        check(verified.valid && verified.record_count == 160U &&
                  verified.proven_prime_count == known_primes.size() &&
                  independent.run_batch_calls() == 1U &&
                  independent.run_calls() == 0U &&
                  independent.last_batch_size() == first.externally_classified_count,
              "final manifest, witnesses, certificates and independent verdicts verify");
        KnownEngine disagreeing_replay{
            "known-disagreeing-replay", known_primes, false, true};
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::verify_campaign(
                    first.results_path, sha256, certificate_verifier,
                    disagreeing_replay));
            },
            "independent batch replay rejects a divergent verdict");
        check(disagreeing_replay.run_batch_calls() == 1U &&
                  disagreeing_replay.run_calls() == 0U,
              "divergent campaign replay still uses exactly one batch call");
        {
            std::ofstream mutation{
                first.flint_evidence_path, std::ios::binary | std::ios::app};
            mutation << "MUTATION\n";
        }
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::verify_campaign(
                    first.results_path, sha256, certificate_verifier, independent));
            },
            "manifest detects a mutated FLINT evidence journal");
        write_file(first.flint_evidence_path, first_flint_bytes);
        {
            std::ofstream mutation{first.results_path, std::ios::binary | std::ios::app};
            mutation << "MUTATION\n";
        }
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::verify_campaign(
                    first.results_path, sha256, certificate_verifier, independent));
            },
            "manifest detects a mutated result ledger");

        std::filesystem::remove_all(config.output_directory);
        primeforge::mvp::SearchExecutionOptions parallel_options;
        parallel_options.native_proof_workers = 4U;
        const auto second = primeforge::mvp::execute_search(
            config, sha256, proof, independent, parallel_options);
        check(read_file(second.results_path) == first_bytes &&
                  read_file(second.flint_evidence_path) == first_flint_bytes &&
                  read_file(second.manifest_path) == first_manifest &&
                  snapshot_regular_files(second.output_directory / "proofs" / "proth") ==
                      first_certificates &&
                  !contains_atomic_temporary_file(second.output_directory),
              "parallel proof workers preserve byte-identical results, manifest, and certificates");

        std::filesystem::remove_all(config.output_directory);
        primeforge::mvp::SearchExecutionOptions stop_options;
        stop_options.clean_stop_after_candidates = 37U;
        stop_options.native_proof_workers = 4U;
        const auto interrupted = primeforge::mvp::execute_search(
            config, sha256, proof, independent, stop_options);
        check(!interrupted.completed &&
                  std::ranges::count(read_file(interrupted.results_path), '\n') == 37 &&
                  std::filesystem::is_regular_file(interrupted.checkpoint_path) &&
                  !std::filesystem::exists(interrupted.manifest_path),
              "clean interruption leaves an exact resumable prefix");
        const auto orphan = config.output_directory / "proofs" / "proth" /
                            "proth-37.json";
        std::filesystem::create_directories(orphan.parent_path());
        { std::ofstream output{orphan, std::ios::binary}; output << "ORPHAN"; }
        const auto interrupted_results_prefix = read_file(interrupted.results_path);
        const auto interrupted_flint_before_injection =
            read_file(interrupted.flint_evidence_path);
        primeforge::mvp::FlintEvidenceLog interrupted_flint_log{
            interrupted.flint_evidence_path, sha256};
        static_cast<void>(interrupted_flint_log.append(
            {"flint-orphan", "1", "COMPOSITE\n", ""}));
        {
            std::ofstream result_tail{
                interrupted.results_path, std::ios::binary | std::ios::app};
            result_tail << "UNAUTHENTICATED-RESULT-TAIL";
        }
        check(read_file(interrupted.results_path) != interrupted_results_prefix &&
                  read_file(interrupted.flint_evidence_path) !=
                      interrupted_flint_before_injection,
              "test injected unauthenticated result and FLINT journal tails");
        primeforge::mvp::SearchExecutionOptions resume_options;
        resume_options.resume_existing = true;
        resume_options.native_proof_workers = 4U;
        const auto resumed = primeforge::mvp::execute_search(
            config, sha256, proof, independent, resume_options);
        check(resumed.completed && read_file(resumed.results_path) == first_bytes &&
                  read_file(resumed.flint_evidence_path) == first_flint_bytes &&
                  read_file(resumed.manifest_path) == first_manifest &&
                  snapshot_regular_files(resumed.output_directory / "proofs" / "proth") ==
                      first_certificates &&
                  !contains_atomic_temporary_file(resumed.output_directory),
              "interruption truncates both tails and reproduces uninterrupted ledgers");
        check(!std::filesystem::exists(orphan) || read_file(orphan) != "ORPHAN",
              "resume removes unauthenticated native proof suffix artifacts");

        std::filesystem::remove_all(config.output_directory);
        const auto short_journal_campaign = primeforge::mvp::execute_search(
            config, sha256, proof, independent, stop_options);
        primeforge::runtime::CheckpointManager checkpoint_manager{sha256};
        const auto short_checkpoint_state =
            checkpoint_manager.load(short_journal_campaign.checkpoint_path);
        const auto authenticated_flint_bytes = checkpoint_decimal(
            short_checkpoint_state.opaque_payload, "flint_evidence_bytes");
        check(authenticated_flint_bytes > 0U,
              "interrupted fixture authenticates a nonempty FLINT journal prefix");
        std::filesystem::resize_file(
            short_journal_campaign.flint_evidence_path,
            authenticated_flint_bytes - 1U);
        const auto short_checkpoint_bytes =
            read_file(short_journal_campaign.checkpoint_path);
        const auto short_results_bytes = read_file(short_journal_campaign.results_path);
        const auto short_flint_bytes =
            read_file(short_journal_campaign.flint_evidence_path);
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::execute_search(
                    config, sha256, proof, independent, resume_options));
            },
            "resume rejects a FLINT journal shorter than its authenticated prefix");
        check(read_file(short_journal_campaign.checkpoint_path) == short_checkpoint_bytes &&
                  read_file(short_journal_campaign.results_path) == short_results_bytes &&
                  read_file(short_journal_campaign.flint_evidence_path) == short_flint_bytes,
              "short journal rejection performs no campaign mutation");

        std::filesystem::remove_all(config.output_directory);
        const auto corrupt_journal_campaign = primeforge::mvp::execute_search(
            config, sha256, proof, independent, stop_options);
        const auto corrupt_checkpoint_bytes =
            read_file(corrupt_journal_campaign.checkpoint_path);
        const auto corrupt_results_bytes = read_file(corrupt_journal_campaign.results_path);
        auto corrupt_flint_bytes = read_file(corrupt_journal_campaign.flint_evidence_path);
        const auto corrupt_checkpoint_state =
            checkpoint_manager.load(corrupt_journal_campaign.checkpoint_path);
        const auto corrupt_authenticated_bytes = checkpoint_decimal(
            corrupt_checkpoint_state.opaque_payload, "flint_evidence_bytes");
        check(corrupt_authenticated_bytes > 0U &&
                  corrupt_authenticated_bytes <= corrupt_flint_bytes.size(),
              "corruption fixture exposes its authenticated FLINT prefix");
        corrupt_flint_bytes.front() = corrupt_flint_bytes.front() == '{' ? '[' : '{';
        write_file(corrupt_journal_campaign.flint_evidence_path, corrupt_flint_bytes);
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::execute_search(
                    config, sha256, proof, independent, resume_options));
            },
            "resume rejects corruption inside the authenticated FLINT prefix");
        check(read_file(corrupt_journal_campaign.checkpoint_path) ==
                      corrupt_checkpoint_bytes &&
                  read_file(corrupt_journal_campaign.results_path) ==
                      corrupt_results_bytes &&
                  read_file(corrupt_journal_campaign.flint_evidence_path) ==
                      corrupt_flint_bytes,
              "corrupt journal rejection performs no campaign mutation");

        std::filesystem::remove_all(config.output_directory);
        primeforge::mvp::SearchExecutionOptions empty_prefix_options;
        empty_prefix_options.clean_stop_after_candidates = 0U;
        const auto empty_prefix = primeforge::mvp::execute_search(
            config, sha256, proof, independent, empty_prefix_options);
        check(!empty_prefix.completed &&
                  std::filesystem::is_regular_file(empty_prefix.results_path) &&
                  read_file(empty_prefix.results_path).empty() &&
                  !std::filesystem::exists(
                      std::filesystem::path{empty_prefix.results_path.string() + ".new"}),
              "zero-length prefix durably creates an empty resumable result ledger");
        const auto checkpoint_before_write_failure = read_file(empty_prefix.checkpoint_path);
        const auto empty_flint_prefix = read_file(empty_prefix.flint_evidence_path);
        primeforge::mvp::SearchExecutionOptions legacy_resume;
        legacy_resume.resume_existing = true;
        const auto canonical_checkpoint =
            checkpoint_manager.load(empty_prefix.checkpoint_path);
        auto uppercase_checkpoint = canonical_checkpoint;
        constexpr std::string_view evidence_digest_marker{
            "flint_evidence_sha256="};
        const auto evidence_digest_position =
            uppercase_checkpoint.opaque_payload.find(evidence_digest_marker);
        check(evidence_digest_position != std::string::npos,
              "checkpoint fixture contains FLINT evidence digest");
        const auto evidence_digest_begin =
            evidence_digest_position + evidence_digest_marker.size();
        for (std::size_t index = evidence_digest_begin;
             index < evidence_digest_begin + 64U; ++index) {
            auto& character = uppercase_checkpoint.opaque_payload[index];
            if (character >= 'a' && character <= 'f') {
                character = static_cast<char>(character - 'a' + 'A');
            }
        }
        check(uppercase_checkpoint.opaque_payload != canonical_checkpoint.opaque_payload,
              "checkpoint fixture produced a noncanonical uppercase SHA-256");
        checkpoint_manager.save(empty_prefix.checkpoint_path, uppercase_checkpoint);
        const auto uppercase_checkpoint_bytes = read_file(empty_prefix.checkpoint_path);
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::execute_search(
                    config, sha256, proof, independent, legacy_resume));
            },
            "pipeline rejects uppercase checkpoint SHA-256 text");
        check(read_file(empty_prefix.checkpoint_path) == uppercase_checkpoint_bytes &&
                  read_file(empty_prefix.results_path).empty() &&
                  read_file(empty_prefix.flint_evidence_path) == empty_flint_prefix,
              "noncanonical SHA-256 rejection performs no campaign mutation");
        write_file(empty_prefix.checkpoint_path, checkpoint_before_write_failure);

        auto legacy_checkpoint = canonical_checkpoint;
        legacy_checkpoint.opaque_payload =
            "configuration_sha256=" + empty_prefix.plan.configuration_sha256 +
            ";results_bytes=0;results_sha256=" + hash_text("", sha256) +
            ";schema=primeforge.mvp.checkpoint.v1";
        checkpoint_manager.save(empty_prefix.checkpoint_path, legacy_checkpoint);
        const auto legacy_checkpoint_bytes = read_file(empty_prefix.checkpoint_path);
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::execute_search(
                    config, sha256, proof, independent, legacy_resume));
            },
            "pipeline v3 explicitly rejects a v1 checkpoint payload");
        check(read_file(empty_prefix.checkpoint_path) == legacy_checkpoint_bytes &&
                  read_file(empty_prefix.results_path).empty() &&
                  read_file(empty_prefix.flint_evidence_path) == empty_flint_prefix,
              "legacy checkpoint rejection performs no campaign mutation");
        write_file(empty_prefix.checkpoint_path, checkpoint_before_write_failure);
        const auto write_obstruction = config.output_directory / "proofs" / "proth" /
                                       "segment-0-16.jsonl";
        std::filesystem::create_directories(write_obstruction);
        {
            std::ofstream marker{write_obstruction / "keep"};
            marker << "OBSTRUCT";
        }
        primeforge::mvp::SearchExecutionOptions failing_parallel_resume;
        failing_parallel_resume.resume_existing = true;
        failing_parallel_resume.native_proof_workers = 4U;
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::execute_search(
                    config, sha256, proof, independent, failing_parallel_resume));
            },
            "parallel certificate write failure propagates to the campaign");
        check(read_file(empty_prefix.checkpoint_path) == checkpoint_before_write_failure &&
                  read_file(empty_prefix.results_path).empty() &&
                  !std::filesystem::exists(empty_prefix.manifest_path),
              "parallel write failure leaves result prefix and checkpoint uncommitted");
        primeforge::mvp::FlintEvidenceLog failed_write_log{
            empty_prefix.flint_evidence_path, sha256};
        check(failed_write_log.authenticate_prefix(0U).sha256 == hash_text("", sha256),
              "parallel write failure preserves the authenticated empty journal prefix");
        std::filesystem::remove_all(write_obstruction);
        const auto recovered_after_write_failure = primeforge::mvp::execute_search(
            config, sha256, proof, independent, failing_parallel_resume);
        check(recovered_after_write_failure.completed &&
                  read_file(recovered_after_write_failure.results_path) == first_bytes &&
                  read_file(recovered_after_write_failure.flint_evidence_path) ==
                      first_flint_bytes &&
                  read_file(recovered_after_write_failure.manifest_path) == first_manifest &&
                  snapshot_regular_files(recovered_after_write_failure.output_directory /
                                         "proofs" / "proth") == first_certificates &&
                  !contains_atomic_temporary_file(
                      recovered_after_write_failure.output_directory),
              "resume after parallel write failure reproduces the complete campaign exactly");

        std::filesystem::remove_all(config.output_directory);
        std::filesystem::remove(first.timeline_json_path);
        std::filesystem::remove(first.timeline_svg_path);
        if (std::filesystem::is_directory(first.timeline_json_path.parent_path()) &&
            std::filesystem::is_empty(first.timeline_json_path.parent_path())) {
            std::filesystem::remove(first.timeline_json_path.parent_path());
        }
        config.output_directory = "mvp-pipeline-mixed-evidence-output";
        KnownEngine mixed_evidence{
            "known-mixed-independent", known_primes, false, false, true};
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::execute_search(
                    config, sha256, proof, mixed_evidence));
            },
            "independent engine cannot mix inline bytes with raw-output paths");
        check(std::filesystem::is_regular_file(
                  config.output_directory / "campaign.checkpoint.json") &&
                  read_file(config.output_directory / "results.jsonl").empty() &&
                  read_file(config.output_directory / "external" / "flint" /
                            "evidence.jsonl").empty() &&
                  !std::filesystem::exists(config.output_directory / "MANIFEST.sha256"),
              "mixed raw-output rejection leaves the initial durable prefix intact");
        std::filesystem::remove_all(config.output_directory);
        config.output_directory = "mvp-pipeline-disagreement-output";
        std::filesystem::remove_all(config.output_directory);
        KnownEngine disagreeing{
            "known-disagreeing-independent", known_primes, false, true};
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::execute_search(
                    config, sha256, proof, disagreeing));
            },
            "independent disagreement fails the campaign");
        check(std::filesystem::exists(config.output_directory / "results.jsonl") &&
                  !std::filesystem::exists(
                      config.output_directory / "MANIFEST.sha256") &&
                  !std::filesystem::exists(
                      config.output_directory / "coverage_report.json"),
              "failed campaign retains a prefix but no finalized artifacts");
        std::filesystem::remove_all(config.output_directory);

        std::cout << "known_candidates=160\n"
                  << "known_proven_primes=" << known_primes.size() << '\n'
                  << "native_proth_certificates=" << native_certificates << '\n'
                  << "deterministic_results=YES\n"
                  << "unique_flint_journal=YES\n"
                  << "interruption_resume_identical=YES\n"
                  << "journal_tail_recovery=YES\n"
                  << "journal_corruption_rejected=YES\n"
                  << "legacy_checkpoint_rejected=YES\n"
                  << "parallel_write_failure_recovery=YES\n"
                  << "batched_independent_replay=YES\n"
                  << "manifest_mutation_rejected=YES\n"
                  << "independent_disagreement_rejected=YES\n"
                  << "PrimeForge MVP pipeline tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge MVP pipeline tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
