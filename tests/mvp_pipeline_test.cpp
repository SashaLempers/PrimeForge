// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/engine/engine_adapter.hpp"
#include "primeforge/mvp/campaign_verifier.hpp"
#include "primeforge/mvp/search_config.hpp"
#include "primeforge/mvp/search_pipeline.hpp"
#include "primeforge/proth/proth.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
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

class KnownEngine final : public primeforge::EngineAdapter {
public:
    KnownEngine(
        std::string id,
        const std::set<std::uint64_t>& known_primes,
        const bool proof,
        const bool disagree = false)
        : id_{std::move(id)}, known_primes_{&known_primes},
          proof_{proof}, disagree_{disagree} {}

    [[nodiscard]] std::string_view id() const noexcept override { return id_; }

    [[nodiscard]] primeforge::EngineCapabilities capabilities() const override {
        return {{"primeforge.proth.uint64.v1"}, false, proof_, false};
    }

    [[nodiscard]] bool supports(
        const primeforge::EngineRequest& request) const noexcept override {
        return request.family_id == "primeforge.proth.uint64.v1";
    }

    [[nodiscard]] primeforge::EngineResult run(
        const primeforge::EngineRequest& request) override {
        const auto value = std::stoull(request.canonical_input);
        bool prime = known_primes_->contains(value);
        if (disagree_ && value == *known_primes_->begin()) prime = !prime;
        const auto directory = request.working_directory / request.job_id;
        std::filesystem::create_directories(directory);
        const auto stdout_path = directory / "stdout.txt";
        const auto stderr_path = directory / "stderr.txt";
        {
            std::ofstream output{stdout_path, std::ios::binary};
            if (proof_) {
                output << (prime ? "PRIMEFORGE:PROVEN_PRIME\n"
                                 : "PRIMEFORGE:COMPOSITE\n");
            } else {
                output << (prime ? "PROVEN_PRIME\n" : "COMPOSITE\n");
            }
        }
        { std::ofstream output{stderr_path, std::ios::binary}; }

        primeforge::EngineResult result;
        result.status.primality = prime ? primeforge::PrimalityStatus::proven_prime
                                        : primeforge::PrimalityStatus::composite;
        result.status.verification = primeforge::VerificationStatus::unverified;
        result.status.novelty = primeforge::NoveltyStatus::not_checked;
        result.diagnostics = prime ? "KNOWN_PROOF_FIXTURE" : "KNOWN_COMPOSITE_FIXTURE";
        result.engine_executable_sha256 = std::string(64U, proof_ ? 'a' : 'b');
        result.raw_stdout_path = stdout_path;
        result.raw_stderr_path = stderr_path;
        if (proof_ && prime) {
            const auto certificate = directory / "certificate.txt";
            std::ofstream output{certificate, std::ios::binary};
            output << "KNOWN-CERTIFICATE:" << value << '\n';
            output.close();
            result.proof_artifact_paths.push_back(certificate);
        }
        return result;
    }

private:
    std::string id_;
    const std::set<std::uint64_t>* known_primes_{};
    bool proof_{};
    bool disagree_{};
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
        config.pari_gp.expected_sha256 = std::string(64U, 'a');
        config.flint.expected_sha256 = std::string(64U, 'b');
        config.output_directory = "mvp-pipeline-test-output";
        std::filesystem::remove_all(config.output_directory);

        KnownEngine proof{"known-pari-proof", known_primes, true};
        KnownEngine independent{"known-flint-independent", known_primes, false};
        const auto first = primeforge::mvp::execute_search(
            config, sha256, proof, independent);
        check(first.completed && first.records.size() == 160U &&
                  first.plan.coverage.valid &&
                  std::filesystem::is_regular_file(first.coverage_report_path) &&
                  std::filesystem::is_regular_file(first.manifest_path),
              "complete exact campaign output");
        check(first.proven_prime_count == known_primes.size() &&
                  first.composite_count == 160U - known_primes.size(),
              "known classification totals");
        check(first.externally_classified_count >= known_primes.size(),
              "every known prime reaches both external contracts");
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
                    read_file(record.native_proth_certificate->artifact_path);
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
        }
        check(native_certificates == known_primes.size(),
              "every known prime uses the native Proth proof boundary");
        const auto first_bytes = read_file(first.results_path);
        check(std::ranges::count(first_bytes, '\n') == 160,
              "one canonical JSONL record per candidate");
        KnownCertificateVerifier certificate_verifier;
        const auto verified = primeforge::mvp::verify_campaign(
            first.results_path, sha256, certificate_verifier, independent);
        check(verified.valid && verified.record_count == 160U &&
                  verified.proven_prime_count == known_primes.size(),
              "final manifest, witnesses, certificates and independent verdicts verify");
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
        const auto second = primeforge::mvp::execute_search(
            config, sha256, proof, independent);
        check(read_file(second.results_path) == first_bytes,
              "repeat search produces byte-identical logical results");

        std::filesystem::remove_all(config.output_directory);
        primeforge::mvp::SearchExecutionOptions stop_options;
        stop_options.clean_stop_after_candidates = 37U;
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
        primeforge::mvp::SearchExecutionOptions resume_options;
        resume_options.resume_existing = true;
        const auto resumed = primeforge::mvp::execute_search(
            config, sha256, proof, independent, resume_options);
        check(resumed.completed && read_file(resumed.results_path) == first_bytes,
              "interruption and resume reproduce uninterrupted logical results");
        check(!std::filesystem::exists(orphan) || read_file(orphan) != "ORPHAN",
              "resume removes unauthenticated native proof suffix artifacts");

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

        std::cout << "known_candidates=160\n"
                  << "known_proven_primes=" << known_primes.size() << '\n'
                  << "native_proth_certificates=" << native_certificates << '\n'
                  << "deterministic_results=YES\n"
                  << "interruption_resume_identical=YES\n"
                  << "manifest_mutation_rejected=YES\n"
                  << "independent_disagreement_rejected=YES\n"
                  << "PrimeForge MVP pipeline tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge MVP pipeline tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
