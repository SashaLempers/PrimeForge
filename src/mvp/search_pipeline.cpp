// SPDX-License-Identifier: Apache-2.0

#include "primeforge/mvp/search_pipeline.hpp"

#include "primeforge/adaptive_bound/adaptive_bound.hpp"
#include "primeforge/congruence/compiler.hpp"
#include "primeforge/family_sieve/family_sieve.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

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

[[nodiscard]] std::string hash_file(
    const std::filesystem::path& path, const Sha256Provider& sha256) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot hash search artifact: " + path.string());
    const std::string content{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return sha256_to_hex(
        sha256.digest(std::as_bytes(std::span{content.data(), content.size()})));
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

void write_results(
    const std::filesystem::path& path,
    const std::vector<SearchRecord>& records,
    const std::filesystem::path& output_directory) {
    const auto temporary = path.string() + ".tmp";
    std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("cannot create temporary results file");
    for (const auto& record : records) {
        output << canonical_search_record(record, output_directory) << '\n';
        if (!output) throw std::runtime_error("cannot write search result");
    }
    output.flush();
    if (!output) throw std::runtime_error("cannot flush search results");
    output.close();
    std::filesystem::rename(temporary, path);
}

}  // namespace

SearchSummary execute_search(
    const SearchConfig& config,
    const Sha256Provider& sha256,
    EngineAdapter& proof_engine,
    EngineAdapter& independent_engine) {
    SearchSummary summary;
    summary.plan = build_campaign_plan(config, sha256);
    summary.output_directory = std::filesystem::absolute(config.output_directory);
    summary.results_path = summary.output_directory / "results.jsonl";
    if (std::filesystem::exists(summary.output_directory)) {
        throw std::invalid_argument("campaign output directory already exists; use resume when available");
    }
    std::filesystem::create_directories(summary.output_directory);

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
    summary.sieve_composite_count = sieve_result.eliminated_count;

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

    summary.records.reserve(static_cast<std::size_t>(summary.plan.candidate_count));
    for (std::uint64_t index = 0U; index < summary.plan.candidate_count; ++index) {
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
            ++summary.composite_count;
            summary.records.push_back(std::move(record));
            continue;
        }

        if (!adaptive_bound::is_base2_strong_probable_prime_u64(record.candidate.value)) {
            record.status.primality = PrimalityStatus::composite;
            record.status.verification = VerificationStatus::self_verified;
            record.classification_method = "BASE2_STRONG_WITNESS";
            record.prp_status = "FAILED";
            ++summary.base2_composite_count;
            ++summary.composite_count;
            summary.records.push_back(std::move(record));
            continue;
        }

        record.status.primality = PrimalityStatus::probable_prime;
        record.status.verification = VerificationStatus::unverified;
        record.prp_status = "PASSED";
        const auto decimal = std::to_string(record.candidate.value);
        const EngineRequest primary_request{
            "pari-" + std::to_string(index), "primeforge.proth.uint64.v1", decimal,
            summary.output_directory / "external" / "pari"};
        if (!proof_engine.supports(primary_request)) {
            throw std::runtime_error("configured proof engine does not support the MVP family");
        }
        const auto primary = proof_engine.run(primary_request);
        if (primary.status.primality != PrimalityStatus::proven_prime &&
            primary.status.primality != PrimalityStatus::composite) {
            throw std::runtime_error("proof engine failed closed: " + primary.diagnostics);
        }
        const bool prime = primary.status.primality == PrimalityStatus::proven_prime;
        record.primary_engine = collect_evidence(proof_engine, primary, sha256, prime);
        record.status.primality = primary.status.primality;
        record.status.verification = prime ? VerificationStatus::self_verified
                                           : VerificationStatus::unverified;
        record.classification_method = prime ? "PARI_PRIMECERT_VALIDATED"
                                             : "PARI_COMPOSITE";

        const EngineRequest independent_request{
            "flint-" + std::to_string(index), "primeforge.proth.uint64.v1", decimal,
            summary.output_directory / "external" / "flint"};
        if (!independent_engine.supports(independent_request)) {
            throw std::runtime_error("configured independent engine does not support the MVP family");
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
        summary.records.push_back(std::move(record));
    }

    if (summary.records.size() != summary.plan.candidate_count ||
        summary.proven_prime_count + summary.composite_count != summary.plan.candidate_count) {
        throw std::logic_error("search accounting is incomplete");
    }
    write_results(summary.results_path, summary.records, summary.output_directory);
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
