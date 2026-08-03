// SPDX-License-Identifier: Apache-2.0

#include "primeforge/mvp/campaign_verifier.hpp"

#include "primeforge/adaptive_bound/adaptive_bound.hpp"
#include "primeforge/engine/external_adapter.hpp"
#include "primeforge/mvp/flint_evidence_log.hpp"
#include "primeforge/mvp/search_config.hpp"
#include "primeforge/proth/proth.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace primeforge::mvp {
namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read verification artifact: " + path.string());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string hash_text(
    const std::string_view content, const Sha256Provider& sha256) {
    return sha256_to_hex(
        sha256.digest(std::as_bytes(std::span{content.data(), content.size()})));
}

[[nodiscard]] std::string hash_file(
    const std::filesystem::path& path, const Sha256Provider& sha256) {
    return hash_text(read_file(path), sha256);
}

[[nodiscard]] std::optional<std::string> string_field(
    const std::string_view object, const std::string_view key) {
    const auto marker = "\"" + std::string{key} + "\":";
    const auto position = object.find(marker);
    if (position == std::string_view::npos) {
        throw std::runtime_error("result record lacks field " + std::string{key});
    }
    const auto begin = position + marker.size();
    if (object.substr(begin, 4U) == "null") return std::nullopt;
    if (begin >= object.size() || object[begin] != '"') {
        throw std::runtime_error("result field is not a JSON string: " + std::string{key});
    }
    std::string result;
    bool escaped = false;
    for (std::size_t index = begin + 1U; index < object.size(); ++index) {
        const char value = object[index];
        if (escaped) {
            if (value != '\\' && value != '"') {
                throw std::runtime_error("unsupported JSON escape in result field");
            }
            result.push_back(value);
            escaped = false;
        } else if (value == '\\') {
            escaped = true;
        } else if (value == '"') {
            return result;
        } else {
            result.push_back(value);
        }
    }
    throw std::runtime_error("truncated JSON string in result record");
}

[[nodiscard]] std::optional<std::string_view> object_field(
    const std::string_view line, const std::string_view key) {
    const auto marker = "\"" + std::string{key} + "\":";
    const auto position = line.find(marker);
    if (position == std::string_view::npos) {
        throw std::runtime_error("result record lacks object " + std::string{key});
    }
    const auto begin = position + marker.size();
    if (line.substr(begin, 4U) == "null") return std::nullopt;
    if (begin >= line.size() || line[begin] != '{') {
        throw std::runtime_error("result evidence is not an object");
    }
    const auto end = line.find('}', begin + 1U);
    if (end == std::string_view::npos) {
        throw std::runtime_error("truncated result evidence object");
    }
    return line.substr(begin, end - begin + 1U);
}

[[nodiscard]] std::uint64_t decimal_field(
    const std::string_view object, const std::string_view key) {
    const auto text = string_field(object, key);
    if (!text.has_value() || text->empty() ||
        (text->size() > 1U && text->front() == '0')) {
        throw std::runtime_error("result decimal field is not canonical");
    }
    std::uint64_t result{};
    const auto parsed = std::from_chars(
        text->data(), text->data() + text->size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size()) {
        throw std::runtime_error("result decimal field is invalid");
    }
    return result;
}

[[nodiscard]] std::filesystem::path safe_artifact_path(
    const std::filesystem::path& campaign_directory, const std::string_view portable) {
    const std::filesystem::path relative{portable};
    if (portable.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.generic_string() != portable) {
        throw std::runtime_error("result contains a non-portable artifact path");
    }
    for (const auto& component : relative) {
        if (component == "..") throw std::runtime_error("result artifact path escapes campaign");
    }
    return campaign_directory / relative;
}

void verify_stored_engine_verdict(
    const std::string_view evidence,
    const std::filesystem::path& campaign_directory,
    const engine::ExternalEngineKind kind,
    const PrimalityStatus expected) {
    if (string_field(evidence, "raw_log_path").has_value() ||
        string_field(evidence, "raw_log_offset").has_value() ||
        string_field(evidence, "raw_log_length").has_value()) {
        throw std::runtime_error("path-backed engine evidence mixes journal fields");
    }
    const auto stdout_text = string_field(evidence, "raw_stdout_path");
    const auto stderr_text = string_field(evidence, "raw_stderr_path");
    if (!stdout_text.has_value() || !stderr_text.has_value()) {
        throw std::runtime_error("stored engine raw-output path is absent");
    }
    const auto stdout_path = safe_artifact_path(campaign_directory, *stdout_text);
    const auto stderr_path = safe_artifact_path(campaign_directory, *stderr_text);
    const auto parsed = engine::parse_external_output(
        kind, "primeforge-external-parser-v1",
        read_file(stdout_path), read_file(stderr_path));
    if (parsed.status.primality != expected) {
        throw std::runtime_error("stored engine output does not reproduce its verdict");
    }
}

[[nodiscard]] FlintEvidenceSlice verify_stored_flint_verdict(
    const std::string_view evidence,
    const std::filesystem::path& campaign_directory,
    const FlintEvidenceLog& journal,
    const PrimalityStatus expected,
    const std::uint64_t flat_index,
    const std::uint64_t value) {
    const auto log_path_text = string_field(evidence, "raw_log_path");
    if (!log_path_text.has_value() ||
        *log_path_text != "external/flint/evidence.jsonl" ||
        string_field(evidence, "raw_stdout_path").has_value() ||
        string_field(evidence, "raw_stderr_path").has_value()) {
        throw std::runtime_error("stored FLINT evidence is not a unique journal slice");
    }
    const auto log_path = safe_artifact_path(campaign_directory, *log_path_text);
    if (std::filesystem::absolute(log_path).lexically_normal() !=
        std::filesystem::absolute(journal.path()).lexically_normal()) {
        throw std::runtime_error("stored FLINT evidence references the wrong journal");
    }
    const FlintEvidenceSlice slice{decimal_field(evidence, "raw_log_offset"),
                                   decimal_field(evidence, "raw_log_length")};
    const auto raw = journal.read(slice);
    if (raw.job_id != "flint-" + std::to_string(flat_index) ||
        raw.input != std::to_string(value)) {
        throw std::runtime_error("stored FLINT evidence is bound to the wrong candidate");
    }
    const auto parsed = engine::parse_external_output(
        engine::ExternalEngineKind::flint, "primeforge-external-parser-v1",
        raw.stdout_bytes, raw.stderr_bytes);
    if (parsed.status.primality != expected) {
        throw std::runtime_error("stored FLINT output does not reproduce its verdict");
    }
    return slice;
}

struct TemporaryDirectory {
    std::filesystem::path path;
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

[[nodiscard]] std::map<std::string, std::string> verify_manifest(
    const std::filesystem::path& campaign_directory,
    const Sha256Provider& sha256) {
    const auto manifest_path = campaign_directory / "MANIFEST.sha256";
    std::ifstream input{manifest_path, std::ios::binary};
    if (!input) throw std::runtime_error("campaign manifest is missing");
    std::map<std::string, std::string> entries;
    std::string line;
    std::string previous;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            throw std::runtime_error("manifest must use LF line endings");
        }
        if (line.size() < 67U || line.substr(64U, 2U) != "  ") {
            throw std::runtime_error("malformed manifest line");
        }
        const auto digest = line.substr(0U, 64U);
        const auto portable = line.substr(66U);
        if (!sha256_from_hex(digest).has_value() || (!previous.empty() && portable <= previous) ||
            !entries.emplace(portable, digest).second) {
            throw std::runtime_error("manifest order, path, or SHA-256 is invalid");
        }
        const auto path = safe_artifact_path(campaign_directory, portable);
        if (!std::filesystem::is_regular_file(path) || hash_file(path, sha256) != digest) {
            throw std::runtime_error("manifest artifact mismatch: " + portable);
        }
        previous = portable;
    }
    std::set<std::string> actual;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             campaign_directory)) {
        if (entry.is_regular_file() && entry.path() != manifest_path) {
            actual.emplace(entry.path().lexically_relative(campaign_directory).generic_string());
        }
    }
    std::set<std::string> declared;
    for (const auto& [path, digest] : entries) {
        static_cast<void>(digest);
        declared.emplace(path);
    }
    if (actual != declared) throw std::runtime_error("manifest inventory is not exact");
    return entries;
}

}  // namespace

VerificationSummary verify_campaign(
    const std::filesystem::path& results_path,
    const Sha256Provider& sha256,
    EngineAdapter& certificate_verifier,
    EngineAdapter& independent_engine) {
    const auto absolute_results = std::filesystem::absolute(results_path).lexically_normal();
    if (absolute_results.filename() != "results.jsonl") {
        throw std::invalid_argument("verify requires a results.jsonl campaign ledger");
    }
    const auto campaign_directory = absolute_results.parent_path();
    const auto manifest = verify_manifest(campaign_directory, sha256);
    constexpr std::string_view flint_journal_portable{
        "external/flint/evidence.jsonl"};
    const auto flint_journal_path =
        campaign_directory / std::filesystem::path{flint_journal_portable};
    if (!manifest.contains(std::string{flint_journal_portable}) ||
        !std::filesystem::is_regular_file(flint_journal_path)) {
        throw std::runtime_error("campaign FLINT evidence journal is missing");
    }
    FlintEvidenceLog flint_journal{flint_journal_path, sha256};
    const auto config = load_search_config(campaign_directory / "search.yaml");
    const auto plan = build_campaign_plan(config, sha256);
    if (std::filesystem::absolute(config.output_directory).lexically_normal() !=
        campaign_directory) {
        throw std::runtime_error("verified configuration output directory mismatch");
    }

    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory temporary{
        std::filesystem::temp_directory_path() / ("primeforge-verify-" + unique)};
    std::filesystem::create_directories(temporary.path);

    VerificationSummary summary;
    summary.campaign_id = plan.campaign_id;
    summary.manifest_file_count = manifest.size();
    std::ifstream results{absolute_results, std::ios::binary};
    if (!results) throw std::runtime_error("results ledger is missing");
    std::vector<FlintEvidenceSlice> flint_slices;
    std::vector<EngineRequest> independent_requests;
    std::vector<PrimalityStatus> independent_expected;
    const auto queue_independent_replay =
        [&](const std::string_view evidence, const std::uint64_t index,
            const std::uint64_t value, const PrimalityStatus expected) {
            flint_slices.push_back(verify_stored_flint_verdict(
                evidence, campaign_directory, flint_journal, expected, index, value));
            independent_requests.push_back(
                {"independent-" + std::to_string(index),
                 "primeforge.proth.uint64.v1", std::to_string(value),
                 temporary.path / "flint"});
            independent_expected.push_back(expected);
        };
    std::string line;
    while (std::getline(results, line)) {
        if (!line.empty() && line.back() == '\r') {
            throw std::runtime_error("results ledger must use LF line endings");
        }
        const auto index = decimal_field(line, "flat_index");
        if (index != summary.record_count || index >= plan.candidate_count) {
            throw std::runtime_error("results ledger has a gap, duplicate, or reordering");
        }
        const auto expected = candidate_at(config, index);
        if (decimal_field(line, "k") != expected.k ||
            decimal_field(line, "n") != expected.n ||
            decimal_field(line, "value") != expected.value ||
            string_field(line, "campaign_id") != plan.campaign_id ||
            string_field(line, "novelty_status") != "NOT_CHECKED") {
            throw std::runtime_error("result coordinates or campaign identity mismatch");
        }
        const auto primality = string_field(line, "primality_status");
        const auto verification = string_field(line, "verification_status");
        const auto method = string_field(line, "classification_method");
        if (primality == "PROBABLE_PRIME") {
            throw std::runtime_error("completed result presents a PRP without proof");
        }
        if (method == "CONGRUENCE_FACTOR") {
            const auto factor = decimal_field(line, "factor");
            if (primality != "COMPOSITE" || verification != "SELF_VERIFIED" ||
                factor <= 1U || factor >= expected.value || expected.value % factor != 0U) {
                throw std::runtime_error("stored congruence factor is not a proper factor");
            }
            ++summary.composite_count;
        } else if (method == "BASE2_STRONG_WITNESS") {
            if (primality != "COMPOSITE" || verification != "SELF_VERIFIED" ||
                adaptive_bound::is_base2_strong_probable_prime_u64(expected.value)) {
                throw std::runtime_error("stored base-2 composite witness does not reproduce");
            }
            ++summary.composite_count;
        } else if (method == "PROTH_CERTIFICATE_VALIDATED") {
            if (primality != "PROVEN_PRIME" ||
                verification != "INDEPENDENTLY_VERIFIED" ||
                string_field(line, "prp_status") != "PASSED" ||
                object_field(line, "primary_engine").has_value()) {
                throw std::runtime_error("native Proth proof obligations are incomplete");
            }
            const auto proof = object_field(line, "native_proth_certificate");
            const auto independent = object_field(line, "independent_engine");
            if (!proof.has_value() || !independent.has_value() ||
                string_field(*proof, "format_version") != proth::certificate_format ||
                string_field(*independent, "executable_sha256") !=
                    config.flint.expected_sha256) {
                throw std::runtime_error("native Proth proof provenance mismatch");
            }
            queue_independent_replay(
                *independent, index, expected.value, PrimalityStatus::proven_prime);
            const auto artifact_text = string_field(*proof, "artifact_path");
            const auto artifact_digest = string_field(*proof, "artifact_sha256");
            if (!artifact_text.has_value() || !artifact_digest.has_value()) {
                throw std::runtime_error("native Proth certificate artifact is missing");
            }
            const auto artifact = safe_artifact_path(campaign_directory, *artifact_text);
            const auto artifact_bytes = read_file(artifact);
            if (hash_text(artifact_bytes, sha256) != *artifact_digest) {
                throw std::runtime_error("native Proth certificate hash mismatch");
            }
            const auto certificate = proth::parse_canonical_certificate(artifact_bytes);
            if (!certificate.has_value() || certificate->k != expected.k ||
                certificate->n != expected.n || certificate->value != expected.value) {
                throw std::runtime_error("native Proth certificate does not bind the candidate");
            }
            ++summary.proven_prime_count;
        } else if (method == "PARI_PRIMECERT_VALIDATED") {
            if (primality != "PROVEN_PRIME" ||
                verification != "INDEPENDENTLY_VERIFIED" ||
                string_field(line, "prp_status") != "PASSED") {
                throw std::runtime_error("prime result status obligations are incomplete");
            }
            const auto primary = object_field(line, "primary_engine");
            const auto independent = object_field(line, "independent_engine");
            if (!primary.has_value() || !independent.has_value() ||
                string_field(*primary, "executable_sha256") !=
                    config.pari_gp.expected_sha256 ||
                string_field(*independent, "executable_sha256") !=
                    config.flint.expected_sha256) {
                throw std::runtime_error("prime engine provenance mismatch");
            }
            verify_stored_engine_verdict(
                *primary, campaign_directory, engine::ExternalEngineKind::pari_gp,
                PrimalityStatus::proven_prime);
            queue_independent_replay(
                *independent, index, expected.value, PrimalityStatus::proven_prime);
            const auto artifact_text = string_field(*primary, "artifact_path");
            const auto artifact_digest = string_field(*primary, "artifact_sha256");
            if (!artifact_text.has_value() || !artifact_digest.has_value()) {
                throw std::runtime_error("prime proof artifact is missing");
            }
            const auto artifact = safe_artifact_path(campaign_directory, *artifact_text);
            if (hash_file(artifact, sha256) != *artifact_digest) {
                throw std::runtime_error("prime proof artifact hash mismatch");
            }
            const EngineRequest certificate_request{
                "certificate-" + std::to_string(index),
                "primeforge.proth.uint64.v1",
                std::to_string(expected.value) + "|" + artifact.generic_string(),
                temporary.path / "pari"};
            const auto certificate_result = certificate_verifier.run(certificate_request);
            if (certificate_result.status.primality != PrimalityStatus::proven_prime) {
                throw std::runtime_error("stored PARI certificate did not validate");
            }
            ++summary.proven_prime_count;
        } else if (method == "PARI_COMPOSITE") {
            if (primality != "COMPOSITE" ||
                verification != "INDEPENDENTLY_VERIFIED" ||
                string_field(line, "prp_status") != "PASSED") {
                throw std::runtime_error("external composite obligations are incomplete");
            }
            const auto primary = object_field(line, "primary_engine");
            const auto independent = object_field(line, "independent_engine");
            if (!primary.has_value() || !independent.has_value() ||
                string_field(*primary, "executable_sha256") !=
                    config.pari_gp.expected_sha256 ||
                string_field(*independent, "executable_sha256") !=
                    config.flint.expected_sha256) {
                throw std::runtime_error("composite engine provenance mismatch");
            }
            verify_stored_engine_verdict(
                *primary, campaign_directory, engine::ExternalEngineKind::pari_gp,
                PrimalityStatus::composite);
            queue_independent_replay(
                *independent, index, expected.value, PrimalityStatus::composite);
            ++summary.composite_count;
        } else {
            throw std::runtime_error("unknown completed classification method");
        }
        ++summary.record_count;
    }
    if (summary.record_count != plan.candidate_count ||
        summary.composite_count + summary.proven_prime_count != plan.candidate_count) {
        throw std::runtime_error("verified result coverage is incomplete");
    }
    const auto flint_prefix = flint_journal.validate_complete_log(flint_slices);
    if (independent_requests.size() != independent_expected.size() ||
        independent_requests.size() != flint_slices.size()) {
        throw std::logic_error("independent replay provenance is incomplete");
    }
    for (const auto& request : independent_requests) {
        if (!independent_engine.supports(request)) {
            throw std::runtime_error("independent engine does not support campaign replay");
        }
    }
    const auto independent_results = independent_engine.run_batch(independent_requests);
    if (independent_results.size() != independent_expected.size()) {
        throw std::runtime_error("independent batch replay returned the wrong result count");
    }
    for (std::size_t index = 0U; index < independent_results.size(); ++index) {
        if (independent_results[index].status.primality != independent_expected[index]) {
            throw std::runtime_error("independent batch replay disagrees with the campaign");
        }
    }
    runtime::CheckpointManager checkpoint_manager{sha256};
    const auto checkpoint = checkpoint_manager.load(
        campaign_directory / "campaign.checkpoint.json");
    if (checkpoint.campaign_id != plan.campaign_id ||
        checkpoint.progress_decimal != std::to_string(plan.candidate_count) ||
        checkpoint.sequence != plan.candidate_count) {
        throw std::runtime_error("final checkpoint does not cover the campaign");
    }
    const auto results_bytes = read_file(absolute_results);
    const auto expected_checkpoint_payload =
        "configuration_sha256=" + plan.configuration_sha256 +
        ";flint_evidence_bytes=" + std::to_string(flint_prefix.size) +
        ";flint_evidence_sha256=" + flint_prefix.sha256 +
        ";results_bytes=" + std::to_string(results_bytes.size()) +
        ";results_sha256=" + hash_text(results_bytes, sha256) +
        ";schema=primeforge.mvp.checkpoint.v2";
    if (checkpoint.opaque_payload != expected_checkpoint_payload) {
        throw std::runtime_error("final checkpoint does not authenticate campaign ledgers");
    }
    const std::string expected_coverage =
        "{\"campaign_id\":\"" + plan.campaign_id +
        "\",\"candidate_count\":\"" + std::to_string(plan.candidate_count) +
        "\",\"composite_count\":\"" + std::to_string(summary.composite_count) +
        "\",\"coverage\":\"EXACT\",\"proven_prime_count\":\"" +
        std::to_string(summary.proven_prime_count) + "\",\"record_count\":\"" +
        std::to_string(summary.record_count) + "\",\"work_unit_count\":\"" +
        std::to_string(plan.work_units.size()) + "\"}";
    if (read_file(campaign_directory / "coverage_report.json") != expected_coverage) {
        throw std::runtime_error("coverage report does not match verified records");
    }
    summary.valid = true;
    return summary;
}

}  // namespace primeforge::mvp
