// SPDX-License-Identifier: Apache-2.0

#include "primeforge/mvp/search_pipeline.hpp"

#include "primeforge/adaptive_bound/adaptive_bound.hpp"
#include "primeforge/congruence/compiler.hpp"
#include "primeforge/family_sieve/family_sieve.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"
#include "primeforge/sieve/sieve.hpp"
#include "primeforge/work/work_unit.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace primeforge::mvp {
namespace {

[[nodiscard]] std::string quote_json(const std::string_view value) {
    std::string result{"\""};
    for (const unsigned char byte : value) {
        if (byte == '"') result += "\\\"";
        else if (byte == '\\') result += "\\\\";
        else if (byte < 0x20U || byte >= 0x7fU) {
            throw std::invalid_argument("MVP result strings must use printable ASCII");
        } else {
            result.push_back(static_cast<char>(byte));
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read search artifact: " + path.string());
    return {
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string hash_text(
    const std::string_view content, const Sha256Provider& sha256) {
    return sha256_to_hex(
        sha256.digest(std::as_bytes(std::span{content.data(), content.size()})));
}

[[nodiscard]] std::string hash_file(
    const std::filesystem::path& path, const Sha256Provider& sha256) {
    const auto content = read_file(path);
    return hash_text(content, sha256);
}

[[nodiscard]] bool eliminated(
    const family_sieve::Result& sieve_result, const std::uint64_t flat_index) {
    return (sieve_result.eliminated_words.at(static_cast<std::size_t>(flat_index / 64U)) &
            (std::uint64_t{1} << (flat_index % 64U))) != 0U;
}

[[nodiscard]] std::string owner_for(
    const CampaignPlan& plan, const std::uint64_t flat_index) {
    for (const auto& unit : plan.work_units) {
        if (flat_index >= unit.interval.begin && flat_index < unit.interval.end) {
            return unit.work_unit_id;
        }
    }
    throw std::logic_error("candidate lacks a work-unit owner");
}

[[nodiscard]] std::string portable_relative(
    const std::filesystem::path& path,
    const std::filesystem::path& output_directory) {
    if (path.empty()) return {};
    const auto absolute_path = std::filesystem::absolute(path).lexically_normal();
    const auto absolute_output = std::filesystem::absolute(output_directory).lexically_normal();
    const auto relative = absolute_path.lexically_relative(absolute_output);
    if (relative.empty() || *relative.begin() == "..") {
        throw std::runtime_error("engine artifact escaped the campaign directory");
    }
    return relative.generic_string();
}

[[nodiscard]] std::string evidence_json(
    const std::optional<EngineEvidence>& evidence,
    const std::filesystem::path& output_directory) {
    if (!evidence.has_value()) return "null";
    const auto artifact_path = portable_relative(
        evidence->proof_artifact_path, output_directory);
    return "{\"artifact_path\":" +
           (artifact_path.empty() ? std::string{"null"} : quote_json(artifact_path)) +
           ",\"artifact_sha256\":" +
           (evidence->proof_artifact_sha256.empty()
                ? std::string{"null"}
                : quote_json(evidence->proof_artifact_sha256)) +
           ",\"engine_id\":" + quote_json(evidence->engine_id) +
           ",\"executable_sha256\":" + quote_json(evidence->executable_sha256) +
           ",\"raw_stderr_path\":" +
           quote_json(portable_relative(evidence->raw_stderr_path, output_directory)) +
           ",\"raw_stdout_path\":" +
           quote_json(portable_relative(evidence->raw_stdout_path, output_directory)) + "}";
}

[[nodiscard]] EngineEvidence collect_evidence(
    const EngineAdapter& adapter,
    const EngineResult& result,
    const Sha256Provider& sha256,
    const bool require_proof) {
    if (!std::filesystem::is_regular_file(result.raw_stdout_path) ||
        !std::filesystem::is_regular_file(result.raw_stderr_path)) {
        throw std::runtime_error(std::string{adapter.id()} + " omitted raw process output");
    }
    EngineEvidence evidence;
    evidence.engine_id = adapter.id();
    if (!sha256_from_hex(result.engine_executable_sha256).has_value()) {
        throw std::runtime_error(std::string{adapter.id()} +
                                 " omitted the executable SHA-256");
    }
    evidence.executable_sha256 = result.engine_executable_sha256;
    evidence.raw_stdout_path = result.raw_stdout_path;
    evidence.raw_stderr_path = result.raw_stderr_path;
    if (require_proof) {
        if (result.proof_artifact_paths.size() != 1U ||
            !std::filesystem::is_regular_file(result.proof_artifact_paths.front())) {
            throw std::runtime_error(std::string{adapter.id()} +
                                     " did not produce exactly one proof artifact");
        }
        evidence.proof_artifact_path = result.proof_artifact_paths.front();
        evidence.proof_artifact_sha256 = hash_file(evidence.proof_artifact_path, sha256);
    }
    return evidence;
}

void write_atomic(const std::filesystem::path& path, const std::string_view content) {
    work::write_checkpoint_atomically(path, std::string{content});
}

void append_durably(const std::filesystem::path& path, const std::string_view content) {
#if defined(_WIN32)
    std::FILE* file{};
    if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || file == nullptr) {
        throw std::runtime_error("cannot append search result");
    }
#else
    std::FILE* file = std::fopen(path.c_str(), "ab");
    if (file == nullptr) throw std::runtime_error("cannot append search result");
#endif
    const bool written = std::fwrite(content.data(), 1U, content.size(), file) == content.size();
    const bool flushed = written && std::fflush(file) == 0;
#if defined(_WIN32)
    const bool committed = flushed && _commit(_fileno(file)) == 0;
#else
    const bool committed = flushed && fsync(fileno(file)) == 0;
#endif
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !committed || !closed) {
        throw std::runtime_error("cannot durably append search result");
    }
}

struct ProgressPayload {
    std::string configuration_sha256;
    std::uint64_t results_bytes{};
    std::string results_sha256;
};

[[nodiscard]] std::uint64_t parse_decimal(
    const std::string_view text, const std::string_view field) {
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        throw std::runtime_error(std::string{field} + " is not canonical decimal");
    }
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::runtime_error(std::string{field} + " is not canonical decimal");
    }
    return value;
}

[[nodiscard]] std::string progress_payload(const ProgressPayload& payload) {
    return "configuration_sha256=" + payload.configuration_sha256 +
           ";results_bytes=" + std::to_string(payload.results_bytes) +
           ";results_sha256=" + payload.results_sha256 +
           ";schema=primeforge.mvp.checkpoint.v1";
}

[[nodiscard]] ProgressPayload parse_progress_payload(const std::string_view payload) {
    constexpr std::string_view configuration_marker{"configuration_sha256="};
    constexpr std::string_view bytes_marker{";results_bytes="};
    constexpr std::string_view digest_marker{";results_sha256="};
    constexpr std::string_view suffix{";schema=primeforge.mvp.checkpoint.v1"};
    if (!payload.starts_with(configuration_marker) || !payload.ends_with(suffix)) {
        throw std::runtime_error("checkpoint payload schema mismatch");
    }
    const auto bytes_position = payload.find(bytes_marker, configuration_marker.size());
    const auto digest_position = payload.find(digest_marker, bytes_position + bytes_marker.size());
    if (bytes_position == std::string_view::npos || digest_position == std::string_view::npos) {
        throw std::runtime_error("checkpoint payload is incomplete");
    }
    ProgressPayload result;
    result.configuration_sha256 = std::string{payload.substr(
        configuration_marker.size(), bytes_position - configuration_marker.size())};
    result.results_bytes = parse_decimal(
        payload.substr(bytes_position + bytes_marker.size(),
                       digest_position - bytes_position - bytes_marker.size()),
        "checkpoint results_bytes");
    result.results_sha256 = std::string{payload.substr(
        digest_position + digest_marker.size(),
        payload.size() - digest_position - digest_marker.size() - suffix.size())};
    if (!sha256_from_hex(result.configuration_sha256).has_value() ||
        !sha256_from_hex(result.results_sha256).has_value()) {
        throw std::runtime_error("checkpoint payload contains invalid SHA-256");
    }
    return result;
}

void save_progress(
    const SearchSummary& summary,
    const std::uint64_t next_index,
    const Sha256Provider& sha256) {
    const auto results = read_file(summary.results_path);
    const ProgressPayload payload{
        summary.plan.configuration_sha256,
        static_cast<std::uint64_t>(results.size()),
        hash_text(results, sha256)};
    runtime::CheckpointManager manager{sha256};
    manager.save(summary.checkpoint_path,
                 {summary.plan.campaign_id, progress_payload(payload),
                  std::to_string(next_index), next_index});
}

void validate_result_prefix(
    const std::string_view prefix,
    const std::uint64_t expected_records,
    const std::string_view campaign_id) {
    std::size_t offset = 0U;
    for (std::uint64_t index = 0U; index < expected_records; ++index) {
        const auto end = prefix.find('\n', offset);
        if (end == std::string_view::npos) {
            throw std::runtime_error("checkpoint result prefix has too few records");
        }
        const auto line = prefix.substr(offset, end - offset);
        const auto campaign_marker = "\"campaign_id\":" + quote_json(campaign_id);
        const auto index_marker = "\"flat_index\":\"" + std::to_string(index) + "\"";
        if (!line.starts_with("{") || !line.ends_with("}") ||
            line.find(campaign_marker) == std::string_view::npos ||
            line.find(index_marker) == std::string_view::npos ||
            line.find('\r') != std::string_view::npos) {
            throw std::runtime_error("checkpoint result prefix is not canonical or contiguous");
        }
        offset = end + 1U;
    }
    if (offset != prefix.size()) {
        throw std::runtime_error("checkpoint result prefix has unexpected records");
    }
}

void account_existing_line(SearchSummary& summary, const std::string_view line) {
    if (line.find("\"primality_status\":\"PROVEN_PRIME\"") != std::string_view::npos) {
        ++summary.proven_prime_count;
    } else if (line.find("\"primality_status\":\"COMPOSITE\"") != std::string_view::npos) {
        ++summary.composite_count;
    } else {
        throw std::runtime_error("checkpoint result has an incomplete primality status");
    }
    if (line.find("\"classification_method\":\"CONGRUENCE_FACTOR\"") !=
        std::string_view::npos) {
        ++summary.sieve_composite_count;
    } else if (line.find("\"classification_method\":\"BASE2_STRONG_WITNESS\"") !=
               std::string_view::npos) {
        ++summary.base2_composite_count;
    } else {
        ++summary.externally_classified_count;
    }
}

[[nodiscard]] std::uint64_t restore_progress(
    SearchSummary& summary, const Sha256Provider& sha256) {
    runtime::CheckpointManager manager{sha256};
    const auto state = manager.load(summary.checkpoint_path);
    if (state.campaign_id != summary.plan.campaign_id ||
        state.sequence != parse_decimal(state.progress_decimal, "checkpoint progress") ||
        state.sequence > summary.plan.candidate_count) {
        throw std::runtime_error("checkpoint campaign or progress mismatch");
    }
    const auto payload = parse_progress_payload(state.opaque_payload);
    if (payload.configuration_sha256 != summary.plan.configuration_sha256) {
        throw std::runtime_error("checkpoint configuration hash mismatch");
    }
    const auto results = read_file(summary.results_path);
    if (payload.results_bytes > results.size()) {
        throw std::runtime_error("results file is shorter than checkpoint");
    }
    const auto prefix = std::string_view{results}.substr(
        0U, static_cast<std::size_t>(payload.results_bytes));
    if (hash_text(prefix, sha256) != payload.results_sha256) {
        throw std::runtime_error("checkpoint results prefix hash mismatch");
    }
    validate_result_prefix(prefix, state.sequence, summary.plan.campaign_id);
    if (payload.results_bytes != results.size()) {
        write_atomic(summary.results_path, prefix);
        for (std::uint64_t index = state.sequence;
             index < summary.plan.candidate_count; ++index) {
            std::filesystem::remove_all(
                summary.output_directory / "external" / "pari" /
                ("pari-" + std::to_string(index)));
            std::filesystem::remove_all(
                summary.output_directory / "external" / "flint" /
                ("flint-" + std::to_string(index)));
        }
    }
    std::size_t offset = 0U;
    for (std::uint64_t index = 0U; index < state.sequence; ++index) {
        const auto end = prefix.find('\n', offset);
        account_existing_line(summary, prefix.substr(offset, end - offset));
        offset = end + 1U;
    }
    return state.sequence;
}

void finalize_campaign(SearchSummary& summary, const Sha256Provider& sha256) {
    const auto record_count = summary.proven_prime_count + summary.composite_count;
    const std::string coverage =
        "{\"campaign_id\":" + quote_json(summary.plan.campaign_id) +
        ",\"candidate_count\":" + quote_json(std::to_string(summary.plan.candidate_count)) +
        ",\"composite_count\":" + quote_json(std::to_string(summary.composite_count)) +
        ",\"coverage\":\"EXACT\",\"proven_prime_count\":" +
        quote_json(std::to_string(summary.proven_prime_count)) +
        ",\"record_count\":" + quote_json(std::to_string(record_count)) +
        ",\"work_unit_count\":" +
        quote_json(std::to_string(summary.plan.work_units.size())) + "}";
    write_atomic(summary.coverage_report_path, coverage);

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             summary.output_directory)) {
        if (entry.is_regular_file() && entry.path() != summary.manifest_path) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files, [&](const auto& left, const auto& right) {
        return portable_relative(left, summary.output_directory) <
               portable_relative(right, summary.output_directory);
    });
    std::string manifest;
    for (const auto& file : files) {
        manifest += hash_file(file, sha256) + "  " +
                    portable_relative(file, summary.output_directory) + "\n";
    }
    write_atomic(summary.manifest_path, manifest);
    summary.completed = true;
}

}  // namespace

SearchSummary execute_search(
    const SearchConfig& config,
    const Sha256Provider& sha256,
    EngineAdapter& proof_engine,
    EngineAdapter& independent_engine,
    const SearchExecutionOptions& execution_options) {
    SearchSummary summary;
    summary.plan = build_campaign_plan(config, sha256);
    summary.output_directory = std::filesystem::absolute(config.output_directory);
    summary.results_path = summary.output_directory / "results.jsonl";
    summary.checkpoint_path = summary.output_directory / "campaign.checkpoint.json";
    summary.coverage_report_path = summary.output_directory / "coverage_report.json";
    summary.manifest_path = summary.output_directory / "MANIFEST.sha256";
    std::uint64_t first_index = 0U;
    if (execution_options.resume_existing) {
        if (!std::filesystem::is_directory(summary.output_directory)) {
            throw std::invalid_argument("resume campaign directory is absent");
        }
        const auto recovery_config = load_search_config(
            summary.output_directory / "search.yaml");
        if (canonical_search_config(recovery_config) != canonical_search_config(config)) {
            throw std::runtime_error("recovery configuration does not match requested campaign");
        }
        first_index = restore_progress(summary, sha256);
    } else {
        if (std::filesystem::exists(summary.output_directory)) {
            throw std::invalid_argument(
                "campaign output directory already exists; use resume");
        }
        std::filesystem::create_directories(summary.output_directory);
        write_atomic(summary.output_directory / "search.yaml",
                     render_search_config_yaml(config));
        {
            std::ofstream empty_results{
                summary.results_path, std::ios::binary | std::ios::trunc};
            if (!empty_results) {
                throw std::runtime_error("cannot create empty results ledger");
            }
        }
        save_progress(summary, 0U, sha256);
    }

    const auto primes = sieve::generate_primes_reference(
        2U, config.sieve_maximum_prime + 1U).primes;
    const auto table = congruence::compile_congruences(
        make_affine_family(config), primes, {}, sha256);
    summary.compiled_table_sha256 = table.table_sha256;

    family_sieve::Options options;
    options.storage = family_sieve::CandidateStorage::dense_bitset;
    options.orientation = family_sieve::BitsetOrientation::by_k;
    options.loop_order = family_sieve::LoopOrder::prime_major;
    options.metadata_layout = family_sieve::MetadataLayout::array_of_structures;
    options.scheduling = family_sieve::Scheduling::static_partition;
    options.vector_mode = family_sieve::VectorMode::scalar;
    options.threads = 1U;
    options.thread_placement = family_sieve::ThreadPlacement::scheduler_managed;
    const auto sieve_result = family_sieve::run(table, sha256, options);
    summary.sieve_result_sha256 = family_sieve::result_sha256(sieve_result, sha256);

    std::map<std::uint64_t, std::uint64_t> factors;
    const auto eliminations = congruence::apply_compiled_table(table, sha256);
    const auto n_count = (config.n_stop - config.n_start) / config.n_step + 1U;
    for (const auto& elimination : eliminations) {
        const auto flat_index = elimination.candidate.k_index * n_count +
                                elimination.candidate.n_index;
        const auto factor = congruence::reconstruct_factor(elimination);
        const auto [position, inserted] = factors.emplace(flat_index, factor);
        if (!inserted) position->second = std::min(position->second, factor);
    }
    for (std::uint64_t index = 0U; index < summary.plan.candidate_count; ++index) {
        if (eliminated(sieve_result, index) != factors.contains(index)) {
            throw std::logic_error("sieve bitset and reconstructed factors disagree");
        }
    }

    summary.records.reserve(static_cast<std::size_t>(
        summary.plan.candidate_count - first_index));
    const auto stop_now = [&]() {
        return execution_options.stop_requested && execution_options.stop_requested();
    };
    if (stop_now() ||
        (execution_options.clean_stop_after_candidates.has_value() &&
         first_index >= *execution_options.clean_stop_after_candidates)) {
        save_progress(summary, first_index, sha256);
        return summary;
    }
    for (std::uint64_t index = first_index;
         index < summary.plan.candidate_count; ++index) {
        SearchRecord record;
        record.campaign_id = summary.plan.campaign_id;
        record.candidate = candidate_at(config, index);
        record.work_unit_id = owner_for(summary.plan, index);
        record.status.novelty = NoveltyStatus::not_checked;
        if (const auto factor = factors.find(index); factor != factors.end()) {
            record.status.primality = PrimalityStatus::composite;
            record.status.verification = VerificationStatus::self_verified;
            record.classification_method = "CONGRUENCE_FACTOR";
            record.prp_status = "NOT_RUN";
            record.factor = factor->second;
            ++summary.sieve_composite_count;
            ++summary.composite_count;
        } else if (!adaptive_bound::is_base2_strong_probable_prime_u64(
                       record.candidate.value)) {
            record.status.primality = PrimalityStatus::composite;
            record.status.verification = VerificationStatus::self_verified;
            record.classification_method = "BASE2_STRONG_WITNESS";
            record.prp_status = "FAILED";
            ++summary.base2_composite_count;
            ++summary.composite_count;
        } else {
            record.status.primality = PrimalityStatus::probable_prime;
            record.status.verification = VerificationStatus::unverified;
            record.prp_status = "PASSED";
            const auto decimal = std::to_string(record.candidate.value);
            const EngineRequest primary_request{
                "pari-" + std::to_string(index), "primeforge.proth.uint64.v1", decimal,
                summary.output_directory / "external" / "pari"};
            if (!proof_engine.supports(primary_request)) {
                throw std::runtime_error(
                    "configured proof engine does not support the MVP family");
            }
            const auto primary = proof_engine.run(primary_request);
            if (primary.status.primality != PrimalityStatus::proven_prime &&
                primary.status.primality != PrimalityStatus::composite) {
                throw std::runtime_error("proof engine failed closed: " +
                                         primary.diagnostics);
            }
            const bool prime =
                primary.status.primality == PrimalityStatus::proven_prime;
            record.primary_engine = collect_evidence(
                proof_engine, primary, sha256, prime);
            record.status.primality = primary.status.primality;
            record.status.verification = prime ? VerificationStatus::self_verified
                                               : VerificationStatus::unverified;
            record.classification_method = prime ? "PARI_PRIMECERT_VALIDATED"
                                                 : "PARI_COMPOSITE";

            const EngineRequest independent_request{
                "flint-" + std::to_string(index), "primeforge.proth.uint64.v1",
                decimal, summary.output_directory / "external" / "flint"};
            if (!independent_engine.supports(independent_request)) {
                throw std::runtime_error(
                    "configured independent engine does not support the MVP family");
            }
            const auto independent = independent_engine.run(independent_request);
            if (independent.status.primality != record.status.primality) {
                throw std::runtime_error("independent engine disagrees at candidate " +
                                         std::to_string(index));
            }
            record.independent_engine = collect_evidence(
                independent_engine, independent, sha256, false);
            record.status.verification = VerificationStatus::independently_verified;
            ++summary.externally_classified_count;
            if (prime) ++summary.proven_prime_count;
            else ++summary.composite_count;
        }

        const auto line = canonical_search_record(record, summary.output_directory) + "\n";
        append_durably(summary.results_path, line);
        summary.records.push_back(std::move(record));
        const auto next_index = index + 1U;
        const bool requested_stop = stop_now() ||
            (execution_options.clean_stop_after_candidates.has_value() &&
             next_index >= *execution_options.clean_stop_after_candidates);
        const bool checkpoint_due = requested_stop ||
            next_index == summary.plan.candidate_count ||
            next_index % config.checkpoint_every_candidates == 0U;
        if (checkpoint_due) save_progress(summary, next_index, sha256);
        if (requested_stop) return summary;
    }

    if (summary.proven_prime_count + summary.composite_count !=
        summary.plan.candidate_count) {
        throw std::logic_error("search accounting is incomplete");
    }
    finalize_campaign(summary, sha256);
    return summary;
}

std::string canonical_search_record(
    const SearchRecord& record, const std::filesystem::path& output_directory) {
    const auto decimal = [](const std::uint64_t value) {
        return quote_json(std::to_string(value));
    };
    return "{\"campaign_id\":" + quote_json(record.campaign_id) +
           ",\"classification_method\":" + quote_json(record.classification_method) +
           ",\"factor\":" +
           (record.factor.has_value() ? decimal(*record.factor) : std::string{"null"}) +
           ",\"flat_index\":" + decimal(record.candidate.flat_index) +
           ",\"independent_engine\":" +
           evidence_json(record.independent_engine, output_directory) +
           ",\"k\":" + decimal(record.candidate.k) +
           ",\"n\":" + decimal(record.candidate.n) +
           ",\"novelty_status\":" + quote_json(to_string(record.status.novelty)) +
           ",\"primality_status\":" + quote_json(to_string(record.status.primality)) +
           ",\"primary_engine\":" +
           evidence_json(record.primary_engine, output_directory) +
           ",\"prp_status\":" + quote_json(record.prp_status) +
           ",\"value\":" + decimal(record.candidate.value) +
           ",\"verification_status\":" +
           quote_json(to_string(record.status.verification)) +
           ",\"work_unit_id\":" + quote_json(record.work_unit_id) + "}";
}

}  // namespace primeforge::mvp
