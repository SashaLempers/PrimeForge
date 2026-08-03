// SPDX-License-Identifier: Apache-2.0

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "primeforge/mvp/search_pipeline.hpp"

#include "primeforge/congruence/compiler.hpp"
#include "primeforge/family_sieve/family_sieve.hpp"
#include "primeforge/mvp/flint_evidence_log.hpp"
#include "primeforge/proth/proth.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"
#include "primeforge/sieve/sieve.hpp"
#include "primeforge/work/work_unit.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(PRIMEFORGE_ENABLE_NVTX)
#include <nvtx3/nvToolsExt.h>
#endif

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace primeforge::mvp {
namespace {

using Clock = std::chrono::steady_clock;

class ScopedTrace {
public:
    explicit ScopedTrace(const char *name) noexcept {
#if defined(PRIMEFORGE_ENABLE_NVTX)
        nvtxRangePushA(name);
#else
        static_cast<void>(name);
#endif
    }
    ~ScopedTrace() {
#if defined(PRIMEFORGE_ENABLE_NVTX)
        nvtxRangePop();
#endif
    }
    ScopedTrace(const ScopedTrace &) = delete;
    ScopedTrace &operator=(const ScopedTrace &) = delete;
};

[[nodiscard]] std::uint64_t elapsed_ns(const Clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
}

template <typename Callable>
[[nodiscard]] auto timed_value(Callable &&callable) {
    const auto started = Clock::now();
    auto value = std::forward<Callable>(callable)();
    return std::pair{elapsed_ns(started), std::move(value)};
}

template <typename Callable>
[[nodiscard]] std::uint64_t timed_action(Callable &&callable) {
    const auto started = Clock::now();
    std::forward<Callable>(callable)();
    return elapsed_ns(started);
}

[[nodiscard]] std::string quote_json(const std::string_view value) {
    std::string result{"\""};
    for (const unsigned char byte : value) {
        if (byte == '"')
            result += "\\\"";
        else if (byte == '\\')
            result += "\\\\";
        else if (byte < 0x20U || byte >= 0x7fU) {
            throw std::invalid_argument("MVP result strings must use printable ASCII");
        } else {
            result.push_back(static_cast<char>(byte));
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read search artifact: " + path.string());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string hash_text(const std::string_view content, const Sha256Provider &sha256) {
    return sha256_to_hex(sha256.digest(std::as_bytes(std::span{content.data(), content.size()})));
}

[[nodiscard]] std::string hash_file(const std::filesystem::path &path,
                                    const Sha256Provider &sha256) {
    const auto content = read_file(path);
    return hash_text(content, sha256);
}

[[nodiscard]] bool eliminated(const family_sieve::Result &sieve_result,
                              const std::uint64_t flat_index) {
    return (sieve_result.eliminated_words.at(static_cast<std::size_t>(flat_index / 64U)) &
            (std::uint64_t{1} << (flat_index % 64U))) != 0U;
}

[[nodiscard]] std::string owner_for(const CampaignPlan &plan, const std::uint64_t flat_index) {
    for (const auto &unit : plan.work_units) {
        if (flat_index >= unit.interval.begin && flat_index < unit.interval.end) {
            return unit.work_unit_id;
        }
    }
    throw std::logic_error("candidate lacks a work-unit owner");
}

[[nodiscard]] std::string portable_relative(const std::filesystem::path &path,
                                            const std::filesystem::path &output_directory) {
    if (path.empty()) return {};
    const auto absolute_path = std::filesystem::absolute(path).lexically_normal();
    const auto absolute_output = std::filesystem::absolute(output_directory).lexically_normal();
    const auto relative = absolute_path.lexically_relative(absolute_output);
    if (relative.empty() || *relative.begin() == "..") {
        throw std::runtime_error("engine artifact escaped the campaign directory");
    }
    return relative.generic_string();
}

[[nodiscard]] std::string evidence_json(const std::optional<EngineEvidence> &evidence,
                                         const std::filesystem::path &output_directory) {
    if (!evidence.has_value()) return "null";
    const auto artifact_path = portable_relative(evidence->proof_artifact_path, output_directory);
    const auto raw_log_path = portable_relative(evidence->raw_log_path, output_directory);
    const auto raw_stderr_path =
        portable_relative(evidence->raw_stderr_path, output_directory);
    const auto raw_stdout_path =
        portable_relative(evidence->raw_stdout_path, output_directory);
    return "{\"artifact_path\":" +
           (artifact_path.empty() ? std::string{"null"} : quote_json(artifact_path)) +
           ",\"artifact_sha256\":" +
           (evidence->proof_artifact_sha256.empty() ? std::string{"null"}
                                                    : quote_json(evidence->proof_artifact_sha256)) +
           ",\"engine_id\":" + quote_json(evidence->engine_id) +
           ",\"executable_sha256\":" + quote_json(evidence->executable_sha256) +
           ",\"raw_log_length\":" +
           (evidence->raw_log_length.has_value()
                ? quote_json(std::to_string(*evidence->raw_log_length))
                : std::string{"null"}) +
           ",\"raw_log_offset\":" +
           (evidence->raw_log_offset.has_value()
                ? quote_json(std::to_string(*evidence->raw_log_offset))
                : std::string{"null"}) +
           ",\"raw_log_path\":" +
           (raw_log_path.empty() ? std::string{"null"} : quote_json(raw_log_path)) +
           ",\"raw_stderr_path\":" +
           (raw_stderr_path.empty() ? std::string{"null"} : quote_json(raw_stderr_path)) +
           ",\"raw_stdout_path\":" +
           (raw_stdout_path.empty() ? std::string{"null"} : quote_json(raw_stdout_path)) + "}";
}

[[nodiscard]] std::string native_proof_json(const std::optional<NativeProofEvidence> &evidence,
                                            const std::filesystem::path &output_directory) {
    if (!evidence.has_value()) return "null";
    return "{\"artifact_path\":" +
           quote_json(portable_relative(evidence->artifact_path, output_directory)) +
           ",\"artifact_sha256\":" + quote_json(evidence->artifact_sha256) +
           ",\"format_version\":" + quote_json(evidence->format_version) + "}";
}

[[nodiscard]] EngineEvidence collect_evidence(const EngineAdapter &adapter,
                                              const EngineResult &result,
                                              const Sha256Provider &sha256,
                                              const bool require_proof) {
    if (!std::filesystem::is_regular_file(result.raw_stdout_path) ||
        !std::filesystem::is_regular_file(result.raw_stderr_path)) {
        throw std::runtime_error(std::string{adapter.id()} + " omitted raw process output");
    }
    EngineEvidence evidence;
    evidence.engine_id = adapter.id();
    if (!sha256_from_hex(result.engine_executable_sha256).has_value()) {
        throw std::runtime_error(std::string{adapter.id()} + " omitted the executable SHA-256");
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

[[nodiscard]] EngineEvidence collect_journal_evidence(
    const EngineAdapter &adapter, const EngineResult &result,
    const std::filesystem::path &journal_path, const FlintEvidenceSlice slice) {
    if (!sha256_from_hex(result.engine_executable_sha256).has_value()) {
        throw std::runtime_error(std::string{adapter.id()} +
                                 " omitted the executable SHA-256");
    }
    if (slice.length == 0U) {
        throw std::runtime_error(std::string{adapter.id()} +
                                 " produced an empty raw-output journal slice");
    }
    EngineEvidence evidence;
    evidence.engine_id = adapter.id();
    evidence.executable_sha256 = result.engine_executable_sha256;
    evidence.raw_log_path = journal_path;
    evidence.raw_log_offset = slice.offset;
    evidence.raw_log_length = slice.length;
    return evidence;
}

[[nodiscard]] FlintEvidenceRecord make_flint_evidence_record(
    const EngineRequest &request, const EngineResult &result) {
    const bool has_inline_stdout = result.raw_stdout_bytes.has_value();
    const bool has_inline_stderr = result.raw_stderr_bytes.has_value();
    if (has_inline_stdout != has_inline_stderr) {
        throw std::runtime_error("independent engine returned incomplete inline raw output");
    }
    if (has_inline_stdout) {
        return {request.job_id, request.canonical_input, *result.raw_stdout_bytes,
                *result.raw_stderr_bytes};
    }
    if (!std::filesystem::is_regular_file(result.raw_stdout_path) ||
        !std::filesystem::is_regular_file(result.raw_stderr_path)) {
        throw std::runtime_error("independent engine omitted raw process output");
    }
    return {request.job_id, request.canonical_input, read_file(result.raw_stdout_path),
            read_file(result.raw_stderr_path)};
}

void write_atomic(const std::filesystem::path &path, const std::string_view content) {
    work::write_checkpoint_atomically(path, std::string{content});
}

void append_durably(const std::filesystem::path &path, const std::string_view content) {
#if defined(_WIN32)
    std::FILE *file{};
    if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || file == nullptr) {
        throw std::runtime_error("cannot append search result");
    }
#else
    std::FILE *file = std::fopen(path.c_str(), "ab");
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
    std::uint64_t flint_evidence_bytes{};
    std::string flint_evidence_sha256;
    std::uint64_t results_bytes{};
    std::string results_sha256;
};

[[nodiscard]] std::uint64_t parse_decimal(const std::string_view text,
                                          const std::string_view field) {
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

[[nodiscard]] std::string progress_payload(const ProgressPayload &payload) {
    return "configuration_sha256=" + payload.configuration_sha256 +
           ";flint_evidence_bytes=" + std::to_string(payload.flint_evidence_bytes) +
           ";flint_evidence_sha256=" + payload.flint_evidence_sha256 +
           ";results_bytes=" + std::to_string(payload.results_bytes) +
           ";results_sha256=" + payload.results_sha256 +
           ";schema=primeforge.mvp.checkpoint.v2";
}

[[nodiscard]] ProgressPayload parse_progress_payload(const std::string_view payload) {
    constexpr std::string_view configuration_marker{"configuration_sha256="};
    constexpr std::string_view evidence_bytes_marker{";flint_evidence_bytes="};
    constexpr std::string_view evidence_digest_marker{";flint_evidence_sha256="};
    constexpr std::string_view bytes_marker{";results_bytes="};
    constexpr std::string_view digest_marker{";results_sha256="};
    constexpr std::string_view suffix{";schema=primeforge.mvp.checkpoint.v2"};
    if (!payload.starts_with(configuration_marker) || !payload.ends_with(suffix)) {
        throw std::runtime_error("checkpoint payload schema mismatch");
    }
    const auto evidence_bytes_position =
        payload.find(evidence_bytes_marker, configuration_marker.size());
    if (evidence_bytes_position == std::string_view::npos) {
        throw std::runtime_error("checkpoint payload is incomplete");
    }
    const auto evidence_digest_position = payload.find(
        evidence_digest_marker, evidence_bytes_position + evidence_bytes_marker.size());
    if (evidence_digest_position == std::string_view::npos) {
        throw std::runtime_error("checkpoint payload is incomplete");
    }
    const auto bytes_position =
        payload.find(bytes_marker, evidence_digest_position + evidence_digest_marker.size());
    if (bytes_position == std::string_view::npos) {
        throw std::runtime_error("checkpoint payload is incomplete");
    }
    const auto digest_position = payload.find(digest_marker, bytes_position + bytes_marker.size());
    if (digest_position == std::string_view::npos) {
        throw std::runtime_error("checkpoint payload is incomplete");
    }
    ProgressPayload result;
    result.configuration_sha256 = std::string{
        payload.substr(configuration_marker.size(),
                       evidence_bytes_position - configuration_marker.size())};
    result.flint_evidence_bytes = parse_decimal(
        payload.substr(evidence_bytes_position + evidence_bytes_marker.size(),
                       evidence_digest_position - evidence_bytes_position -
                           evidence_bytes_marker.size()),
        "checkpoint flint_evidence_bytes");
    result.flint_evidence_sha256 = std::string{payload.substr(
        evidence_digest_position + evidence_digest_marker.size(),
        bytes_position - evidence_digest_position - evidence_digest_marker.size())};
    result.results_bytes =
        parse_decimal(payload.substr(bytes_position + bytes_marker.size(),
                                     digest_position - bytes_position - bytes_marker.size()),
                      "checkpoint results_bytes");
    result.results_sha256 = std::string{
        payload.substr(digest_position + digest_marker.size(),
                       payload.size() - digest_position - digest_marker.size() - suffix.size())};
    if (!sha256_from_hex(result.configuration_sha256).has_value() ||
        !sha256_from_hex(result.flint_evidence_sha256).has_value() ||
        !sha256_from_hex(result.results_sha256).has_value()) {
        throw std::runtime_error("checkpoint payload contains invalid SHA-256");
    }
    if (progress_payload(result) != payload) {
        throw std::runtime_error("checkpoint payload is not canonical");
    }
    return result;
}

void save_progress(const SearchSummary &summary, const std::uint64_t next_index,
                   const Sha256Provider &sha256, const FlintEvidenceLog &flint_log,
                   const std::uint64_t flint_evidence_bytes) {
    const auto results = read_file(summary.results_path);
    const auto evidence_prefix = flint_log.authenticate_prefix(flint_evidence_bytes);
    const ProgressPayload payload{summary.plan.configuration_sha256,
                                  evidence_prefix.size,
                                  evidence_prefix.sha256,
                                  static_cast<std::uint64_t>(results.size()),
                                  hash_text(results, sha256)};
    runtime::CheckpointManager manager{sha256};
    manager.save(summary.checkpoint_path, {summary.plan.campaign_id, progress_payload(payload),
                                           std::to_string(next_index), next_index});
}

struct IndexedFlintEvidenceSlice {
    std::uint64_t flat_index{};
    FlintEvidenceSlice slice;
};

[[nodiscard]] std::uint64_t quoted_decimal_before(
    const std::string_view line, const std::string_view marker,
    const std::size_t before, const std::string_view field) {
    const auto marker_position = line.rfind(marker, before);
    if (marker_position == std::string_view::npos) {
        throw std::runtime_error(std::string{field} + " is absent from result evidence");
    }
    const auto begin = marker_position + marker.size();
    const auto end = line.find('"', begin);
    if (end == std::string_view::npos || end >= before) {
        throw std::runtime_error(std::string{field} + " is malformed in result evidence");
    }
    return parse_decimal(line.substr(begin, end - begin), field);
}

[[nodiscard]] std::optional<FlintEvidenceSlice> flint_slice_from_result(
    const std::string_view line) {
    constexpr std::string_view path_marker{
        "\"raw_log_path\":\"external/flint/evidence.jsonl\""};
    constexpr std::string_view length_marker{"\"raw_log_length\":\""};
    constexpr std::string_view offset_marker{"\"raw_log_offset\":\""};
    const auto path_position = line.find(path_marker);
    if (path_position == std::string_view::npos) return std::nullopt;
    if (line.find(path_marker, path_position + path_marker.size()) != std::string_view::npos) {
        throw std::runtime_error("result contains duplicate FLINT journal references");
    }
    const auto length = quoted_decimal_before(
        line, length_marker, path_position, "result raw_log_length");
    const auto offset = quoted_decimal_before(
        line, offset_marker, path_position, "result raw_log_offset");
    if (length == 0U) {
        throw std::runtime_error("result FLINT journal slice is empty");
    }
    return FlintEvidenceSlice{offset, length};
}

[[nodiscard]] std::vector<IndexedFlintEvidenceSlice> validate_result_prefix(
    const std::string_view prefix, const std::uint64_t expected_records,
    const std::string_view campaign_id) {
    std::vector<IndexedFlintEvidenceSlice> flint_slices;
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
        const auto slice = flint_slice_from_result(line);
        const bool has_independent_evidence =
            line.find("\"independent_engine\":null") == std::string_view::npos;
        if (has_independent_evidence != slice.has_value()) {
            throw std::runtime_error(
                "checkpoint result and FLINT journal provenance disagree");
        }
        if (slice.has_value()) {
            flint_slices.push_back({index, *slice});
        }
        offset = end + 1U;
    }
    if (offset != prefix.size()) {
        throw std::runtime_error("checkpoint result prefix has unexpected records");
    }
    return flint_slices;
}

void account_existing_line(SearchSummary &summary, const std::string_view line) {
    if (line.find("\"primality_status\":\"PROVEN_PRIME\"") != std::string_view::npos) {
        ++summary.proven_prime_count;
    } else if (line.find("\"primality_status\":\"COMPOSITE\"") != std::string_view::npos) {
        ++summary.composite_count;
    } else {
        throw std::runtime_error("checkpoint result has an incomplete primality status");
    }
    if (line.find("\"classification_method\":\"CONGRUENCE_FACTOR\"") != std::string_view::npos) {
        ++summary.sieve_composite_count;
    } else if (line.find("\"classification_method\":\"BASE2_STRONG_WITNESS\"") !=
               std::string_view::npos) {
        ++summary.base2_composite_count;
        ++summary.prp_tested_count;
    } else {
        ++summary.externally_classified_count;
        ++summary.prp_tested_count;
    }
}

struct PreparedPrpBatch {
    std::uint64_t begin{};
    std::uint64_t end{};
    std::vector<std::uint64_t> survivor_values;
    std::vector<std::uint64_t> survivor_offsets;
    std::vector<prp::Base2StrongPrpVerdict> verdicts;
    std::uint64_t packing_ns{};
    std::future<prp::Base2StrongPrpBatchMetrics> completion;
    std::future<std::vector<EngineResult>> independent_completion;
    std::vector<std::size_t> independent_survivors;
    std::vector<EngineRequest> independent_requests;
    std::vector<EngineResult> independent_results;
    std::vector<std::optional<FlintEvidenceSlice>> independent_evidence_slices;
    struct NativeProofBatch {
        std::vector<proth::ProofAttempt> results;
        std::vector<std::optional<NativeProofEvidence>> evidence;
        std::uint64_t elapsed_ns{};
    };
    std::future<NativeProofBatch> native_proof_completion;
    std::vector<proth::ProofAttempt> native_proof_results;
    std::vector<std::optional<NativeProofEvidence>> native_proof_evidence;
    Clock::time_point independent_started{};
    bool independent_submitted{};
    bool independent_completed{};
};

[[nodiscard]] std::unique_ptr<PreparedPrpBatch>
prepare_prp_batch(const SearchConfig &config, const family_sieve::Result &sieve_result,
                  const std::uint64_t begin, const std::uint64_t batch_candidates) {
    const auto packing_started = Clock::now();
    const ScopedTrace trace{"candidate_pack"};
    auto batch = std::make_unique<PreparedPrpBatch>();
    batch->begin = begin;
    const auto total = candidate_count(config);
    batch->end = begin + std::min(batch_candidates, total - begin);
    batch->survivor_values.reserve(static_cast<std::size_t>(batch->end - batch->begin));
    batch->survivor_offsets.reserve(static_cast<std::size_t>(batch->end - batch->begin));
    for (auto index = batch->begin; index < batch->end; ++index) {
        if (sieve_result.factor_witnesses[static_cast<std::size_t>(index)] == 0U) {
            batch->survivor_values.push_back(candidate_at(config, index).value);
            batch->survivor_offsets.push_back(index - batch->begin);
        }
    }
    batch->verdicts.resize(batch->survivor_values.size());
    batch->packing_ns = elapsed_ns(packing_started);
    return batch;
}

void submit_prp_batch(PreparedPrpBatch &batch, prp::Base2StrongPrpBatchBackend &backend,
                      SearchSummary &summary) {
    if (batch.survivor_values.empty()) return;
    ++summary.prp_submitted_batches;
    auto *const values = &batch.survivor_values;
    auto *const verdicts = &batch.verdicts;
    auto *const selected_backend = &backend;
    batch.completion = std::async(std::launch::async, [values, verdicts, selected_backend] {
        const ScopedTrace trace{"prp_batch"};
        return selected_backend->test(*values, *verdicts);
    });
}

void await_prp_batch(PreparedPrpBatch &batch, SearchSummary &summary) {
    if (!batch.completion.valid()) return;
    const auto metrics = batch.completion.get();
    summary.metrics.prp_cpu_ns += metrics.cpu_ns;
    summary.metrics.host_to_device_ns += metrics.host_to_device_ns;
    summary.metrics.kernel_ns += metrics.kernel_ns;
    summary.metrics.device_to_host_ns += metrics.device_to_host_ns;
}

void submit_independent_batch(PreparedPrpBatch &batch, const SearchConfig &config,
                              EngineAdapter &independent_engine,
                              const std::filesystem::path &output_directory) {
    const auto parallelism = independent_engine.recommended_parallelism();
    if (parallelism <= 1U || batch.survivor_values.empty()) return;
    batch.independent_results.resize(batch.verdicts.size());
    batch.independent_evidence_slices.resize(batch.verdicts.size());
    batch.independent_started = Clock::now();
    batch.independent_submitted = true;
    auto &requests = batch.independent_requests;
    for (std::size_t survivor = 0U; survivor < batch.verdicts.size(); ++survivor) {
        if (batch.verdicts[survivor] != prp::Base2StrongPrpVerdict::probable_prime) continue;
        const auto index = batch.begin + batch.survivor_offsets[survivor];
        EngineRequest request{"flint-" + std::to_string(index),
                              "primeforge.proth.uint64.v1",
                              std::to_string(candidate_at(config, index).value),
                              output_directory / "external" / "flint"};
        if (!independent_engine.supports(request)) {
            throw std::runtime_error(
                "configured independent engine does not support the MVP family");
        }
        batch.independent_survivors.push_back(survivor);
        requests.push_back(std::move(request));
    }
    auto *const engine = &independent_engine;
    batch.independent_completion = std::async(
        std::launch::async, [engine, requests = batch.independent_requests]() mutable {
            const ScopedTrace trace{"verification"};
            return engine->run_batch(requests);
        });
}

void await_independent_batch(PreparedPrpBatch &batch, SearchSummary &summary,
                             FlintEvidenceLog &flint_log) {
    if (!batch.independent_submitted) return;
    auto results = batch.independent_completion.get();
    if (results.size() != batch.independent_survivors.size()) {
        throw std::logic_error("independent batch returned the wrong result count");
    }
    if (results.size() != batch.independent_requests.size()) {
        throw std::logic_error("independent batch lost its request provenance");
    }
    std::vector<FlintEvidenceRecord> evidence_records;
    evidence_records.reserve(results.size());
    for (std::size_t index = 0U; index < results.size(); ++index) {
        evidence_records.push_back(
            make_flint_evidence_record(batch.independent_requests[index], results[index]));
    }
    std::vector<FlintEvidenceSlice> slices;
    {
        const ScopedTrace trace{"result_io"};
        summary.metrics.io_ns += timed_action(
            [&] { slices = flint_log.append_batch(evidence_records); });
    }
    if (slices.size() != results.size()) {
        throw std::logic_error("FLINT journal returned the wrong slice count");
    }
    for (std::size_t index = 0U; index < results.size(); ++index) {
        const auto survivor = batch.independent_survivors[index];
        batch.independent_results[survivor] = std::move(results[index]);
        batch.independent_evidence_slices[survivor] = slices[index];
    }
    summary.metrics.verification_ns += elapsed_ns(batch.independent_started);
    batch.independent_submitted = false;
    batch.independent_completed = true;
}

void submit_native_proof_batch(PreparedPrpBatch &batch, const SearchConfig &config,
                               const std::filesystem::path &output_directory,
                               const Sha256Provider &sha256,
                               const std::size_t requested_worker_count) {
    batch.native_proof_completion = std::async(
        std::launch::async,
        [&batch, &config, output_directory, &sha256, requested_worker_count] {
        const ScopedTrace trace{"proof_batch"};
        const auto started = Clock::now();
        PreparedPrpBatch::NativeProofBatch completed;
        completed.results.resize(batch.verdicts.size());
        completed.evidence.resize(batch.verdicts.size());
        std::vector<std::size_t> probable_survivors;
        probable_survivors.reserve(batch.verdicts.size());
        for (std::size_t survivor = 0U; survivor < batch.verdicts.size(); ++survivor) {
            if (batch.verdicts[survivor] == prp::Base2StrongPrpVerdict::probable_prime) {
                probable_survivors.push_back(survivor);
            }
        }

        const auto proof_directory = output_directory / "proofs" / "proth";
        std::filesystem::create_directories(proof_directory);
        const auto worker_count =
            std::min(requested_worker_count, probable_survivors.size());
        std::atomic_size_t next_survivor{0U};
        std::atomic_bool stop_workers{false};
        std::mutex failure_mutex;
        std::exception_ptr first_failure;
        const auto worker = [&] {
            try {
                while (!stop_workers.load(std::memory_order_relaxed)) {
                    const auto queue_index =
                        next_survivor.fetch_add(1U, std::memory_order_relaxed);
                    if (queue_index >= probable_survivors.size()) return;
                    const auto survivor = probable_survivors[queue_index];
                    const auto flat_index = batch.begin + batch.survivor_offsets[survivor];
                    const auto candidate = candidate_at(config, flat_index);
                    auto attempt = proth::try_prove_u64(
                        candidate.k, static_cast<std::uint32_t>(candidate.n),
                        65'535U);
                    if (attempt.certificate.has_value()) {
                        const auto certificate_bytes =
                            proth::canonical_certificate(*attempt.certificate);
                        const auto certificate_path =
                            proof_directory / ("proth-" + std::to_string(flat_index) + ".json");
                        write_atomic(certificate_path, certificate_bytes);
                        completed.evidence[survivor] = NativeProofEvidence{
                            proth::certificate_format, certificate_path,
                            hash_text(certificate_bytes, sha256)};
                    }
                    completed.results[survivor] = std::move(attempt);
                }
            } catch (...) {
                {
                    const std::scoped_lock lock{failure_mutex};
                    if (first_failure == nullptr) first_failure = std::current_exception();
                }
                stop_workers.store(true, std::memory_order_relaxed);
            }
        };
        {
            std::vector<std::jthread> workers;
            workers.reserve(worker_count);
            for (std::size_t index = 0U; index < worker_count; ++index) {
                workers.emplace_back(worker);
            }
        }
        if (first_failure != nullptr) std::rethrow_exception(first_failure);
        completed.elapsed_ns = elapsed_ns(started);
        return completed;
    });
}

void await_native_proof_batch(PreparedPrpBatch &batch, SearchSummary &summary) {
    auto completed = batch.native_proof_completion.get();
    batch.native_proof_results = std::move(completed.results);
    batch.native_proof_evidence = std::move(completed.evidence);
    summary.metrics.proof_ns += completed.elapsed_ns;
}

struct RestoredProgress {
    std::uint64_t next_index{};
    std::uint64_t flint_evidence_bytes{};
};

[[nodiscard]] RestoredProgress restore_progress(
    SearchSummary &summary, const SearchConfig &config, const Sha256Provider &sha256,
    FlintEvidenceLog &flint_log) {
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
    const auto prefix =
        std::string_view{results}.substr(0U, static_cast<std::size_t>(payload.results_bytes));
    if (hash_text(prefix, sha256) != payload.results_sha256) {
        throw std::runtime_error("checkpoint results prefix hash mismatch");
    }
    const auto indexed_slices =
        validate_result_prefix(prefix, state.sequence, summary.plan.campaign_id);
    std::vector<FlintEvidenceSlice> slices;
    slices.reserve(indexed_slices.size());
    for (const auto &indexed : indexed_slices) {
        const auto evidence = flint_log.read(indexed.slice);
        const auto expected_job = "flint-" + std::to_string(indexed.flat_index);
        const auto expected_input =
            std::to_string(candidate_at(config, indexed.flat_index).value);
        if (evidence.job_id != expected_job || evidence.input != expected_input) {
            throw std::runtime_error("checkpoint FLINT evidence is bound to the wrong candidate");
        }
        slices.push_back(indexed.slice);
    }
    const auto evidence_prefix = flint_log.validate_contiguous_prefix(slices);
    if (evidence_prefix.size != payload.flint_evidence_bytes ||
        evidence_prefix.sha256 != payload.flint_evidence_sha256) {
        throw std::runtime_error("checkpoint FLINT evidence prefix mismatch");
    }
    if (payload.results_bytes != results.size()) {
        write_atomic(summary.results_path, prefix);
    }
    flint_log.truncate_authenticated(evidence_prefix);
    const auto flint_root = summary.output_directory / "external" / "flint";
    if (std::filesystem::is_directory(flint_root)) {
        for (const auto &entry : std::filesystem::directory_iterator(flint_root)) {
            const auto name = entry.path().filename().generic_string();
            if (entry.is_directory() &&
                (name.starts_with(".batch-") ||
                 name.starts_with(".primeforge-flint-batch-"))) {
                std::filesystem::remove_all(entry.path());
            }
        }
    }
    for (std::uint64_t index = state.sequence; index < summary.plan.candidate_count; ++index) {
        std::filesystem::remove_all(summary.output_directory / "external" / "pari" /
                                    ("pari-" + std::to_string(index)));
        std::filesystem::remove_all(summary.output_directory / "external" / "flint" /
                                    ("flint-" + std::to_string(index)));
        std::filesystem::remove(summary.output_directory / "proofs" / "proth" /
                                ("proth-" + std::to_string(index) + ".json"));
    }
    std::size_t offset = 0U;
    for (std::uint64_t index = 0U; index < state.sequence; ++index) {
        const auto end = prefix.find('\n', offset);
        account_existing_line(summary, prefix.substr(offset, end - offset));
        offset = end + 1U;
    }
    return {state.sequence, evidence_prefix.size};
}

void finalize_campaign(SearchSummary &summary, const Sha256Provider &sha256) {
    const auto record_count = summary.proven_prime_count + summary.composite_count;
    const std::string coverage =
        "{\"campaign_id\":" + quote_json(summary.plan.campaign_id) +
        ",\"candidate_count\":" + quote_json(std::to_string(summary.plan.candidate_count)) +
        ",\"composite_count\":" + quote_json(std::to_string(summary.composite_count)) +
        ",\"coverage\":\"EXACT\",\"proven_prime_count\":" +
        quote_json(std::to_string(summary.proven_prime_count)) +
        ",\"record_count\":" + quote_json(std::to_string(record_count)) +
        ",\"work_unit_count\":" + quote_json(std::to_string(summary.plan.work_units.size())) + "}";
    write_atomic(summary.coverage_report_path, coverage);

    std::vector<std::filesystem::path> files;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(summary.output_directory)) {
        if (entry.is_regular_file() && entry.path() != summary.manifest_path) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files, [&](const auto &left, const auto &right) {
        return portable_relative(left, summary.output_directory) <
               portable_relative(right, summary.output_directory);
    });
    std::string manifest;
    for (const auto &file : files) {
        manifest += hash_file(file, sha256) + "  " +
                    portable_relative(file, summary.output_directory) + "\n";
    }
    write_atomic(summary.manifest_path, manifest);
    summary.completed = true;
}

}  // namespace

SearchSummary execute_search(const SearchConfig &config, const Sha256Provider &sha256,
                             EngineAdapter &proof_engine, EngineAdapter &independent_engine,
                             const SearchExecutionOptions &execution_options) {
    const auto total_started = Clock::now();
    if (execution_options.prp_batch_candidates == 0U) {
        throw std::invalid_argument("PRP batch candidate count must be nonzero");
    }
    if (execution_options.native_proof_workers == 0U ||
        execution_options.native_proof_workers > 64U) {
        throw std::invalid_argument("native proof worker count must be between 1 and 64");
    }
    auto owned_prp_backend =
        execution_options.prp_backend == nullptr
            ? prp::make_cpu_base2_strong_prp_batch_backend(execution_options.prp_batch_candidates)
            : nullptr;
    auto &prp_backend = execution_options.prp_backend == nullptr ? *owned_prp_backend
                                                                 : *execution_options.prp_backend;
    if (prp_backend.capacity() == 0U) {
        throw std::invalid_argument("PRP backend capacity must be nonzero");
    }
    const auto bounded_batch_candidates = static_cast<std::uint64_t>(
        std::min(execution_options.prp_batch_candidates, prp_backend.capacity()));
    SearchSummary summary;
    summary.prp_backend_id = std::string{prp_backend.id()};
    summary.native_proof_workers = execution_options.native_proof_workers;
    {
        const ScopedTrace trace{"candidate_generation"};
        auto [duration, plan] = timed_value([&] { return build_campaign_plan(config, sha256); });
        summary.metrics.generation_ns += duration;
        summary.plan = std::move(plan);
    }
    summary.output_directory = std::filesystem::absolute(config.output_directory);
    summary.results_path = summary.output_directory / "results.jsonl";
    summary.flint_evidence_path =
        summary.output_directory / "external" / "flint" / "evidence.jsonl";
    summary.checkpoint_path = summary.output_directory / "campaign.checkpoint.json";
    summary.coverage_report_path = summary.output_directory / "coverage_report.json";
    summary.manifest_path = summary.output_directory / "MANIFEST.sha256";
    std::uint64_t first_index = 0U;
    std::uint64_t committed_flint_evidence_bytes = 0U;
    std::unique_ptr<FlintEvidenceLog> flint_log;
    if (execution_options.resume_existing) {
        if (!std::filesystem::is_directory(summary.output_directory)) {
            throw std::invalid_argument("resume campaign directory is absent");
        }
        const auto recovery_config = load_search_config(summary.output_directory / "search.yaml");
        if (canonical_search_config(recovery_config) != canonical_search_config(config)) {
            throw std::runtime_error("recovery configuration does not match requested campaign");
        }
        if (!std::filesystem::is_regular_file(summary.flint_evidence_path)) {
            throw std::runtime_error("checkpoint FLINT evidence journal is missing");
        }
        flint_log = std::make_unique<FlintEvidenceLog>(summary.flint_evidence_path, sha256);
        {
            const ScopedTrace trace{"checkpoint"};
            const auto [duration, restored] =
                timed_value([&] { return restore_progress(summary, config, sha256, *flint_log); });
            summary.metrics.checkpoint_ns += duration;
            first_index = restored.next_index;
            committed_flint_evidence_bytes = restored.flint_evidence_bytes;
        }
    } else {
        if (std::filesystem::exists(summary.output_directory)) {
            throw std::invalid_argument("campaign output directory already exists; use resume");
        }
        std::filesystem::create_directories(summary.output_directory);
        {
            const ScopedTrace trace{"result_io"};
            summary.metrics.io_ns += timed_action([&] {
                write_atomic(summary.output_directory / "search.yaml",
                             render_search_config_yaml(config));
                std::filesystem::create_directories(summary.flint_evidence_path.parent_path());
                flint_log =
                    std::make_unique<FlintEvidenceLog>(summary.flint_evidence_path, sha256);
                std::ofstream empty_results{summary.results_path,
                                            std::ios::binary | std::ios::trunc};
                if (!empty_results) {
                    throw std::runtime_error("cannot create empty results ledger");
                }
            });
        }
        {
            const ScopedTrace trace{"checkpoint"};
            summary.metrics.checkpoint_ns +=
                timed_action([&] {
                    save_progress(summary, 0U, sha256, *flint_log, 0U);
                });
        }
    }

    congruence::CompiledTable table;
    {
        const ScopedTrace trace{"congruence_compile"};
        auto [duration, compiled] = timed_value([&] {
            const auto primes =
                sieve::generate_primes_reference(2U, config.sieve_maximum_prime + 1U).primes;
            return congruence::compile_congruences(
                make_affine_family(config), primes, {}, sha256);
        });
        summary.metrics.congruence_ns += duration;
        table = std::move(compiled);
    }
    summary.compiled_table_sha256 = table.table_sha256;

    family_sieve::Options options;
    options.storage = family_sieve::CandidateStorage::dense_bitset;
    options.orientation = family_sieve::BitsetOrientation::by_k;
    options.loop_order = family_sieve::LoopOrder::prime_major;
    options.metadata_layout = family_sieve::MetadataLayout::array_of_structures;
    options.scheduling = family_sieve::Scheduling::static_partition;
    options.vector_mode = family_sieve::VectorMode::scalar;
    options.threads = select_sieve_threads(
        summary.plan.candidate_count, std::thread::hardware_concurrency());
    summary.sieve_threads = options.threads;
    options.thread_placement = family_sieve::ThreadPlacement::scheduler_managed;
    options.retain_factor_witnesses = true;
    family_sieve::Result sieve_result;
    {
        const ScopedTrace trace{"sieve"};
        auto [duration, result] =
            timed_value([&] { return family_sieve::run(table, sha256, options); });
        summary.metrics.sieve_ns += duration;
        sieve_result = std::move(result);
    }
    summary.sieve_result_sha256 = family_sieve::result_sha256(sieve_result, sha256);
    if (sieve_result.factor_witnesses.size() != summary.plan.candidate_count) {
        throw std::logic_error("sieve did not retain the complete factor-witness vector");
    }
    for (std::uint64_t index = 0U; index < summary.plan.candidate_count; ++index) {
        if (eliminated(sieve_result, index) !=
            (sieve_result.factor_witnesses[static_cast<std::size_t>(index)] != 0U)) {
            throw std::logic_error("sieve bitset and reconstructed factors disagree");
        }
    }

    summary.records.reserve(static_cast<std::size_t>(summary.plan.candidate_count - first_index));
    const auto stop_now = [&]() {
        return execution_options.stop_requested && execution_options.stop_requested();
    };
    if (stop_now() || (execution_options.clean_stop_after_candidates.has_value() &&
                       first_index >= *execution_options.clean_stop_after_candidates)) {
        {
            const ScopedTrace trace{"checkpoint"};
            summary.metrics.checkpoint_ns +=
                timed_action([&] {
                    save_progress(summary, first_index, sha256, *flint_log,
                                  committed_flint_evidence_bytes);
                });
        }
        summary.metrics.total_ns = elapsed_ns(total_started);
        return summary;
    }
    auto current_batch =
        prepare_prp_batch(config, sieve_result, first_index, bounded_batch_candidates);
    summary.metrics.packing_ns += current_batch->packing_ns;
    submit_prp_batch(*current_batch, prp_backend, summary);
    auto next_batch =
        current_batch->end < summary.plan.candidate_count
            ? prepare_prp_batch(config, sieve_result, current_batch->end, bounded_batch_candidates)
            : nullptr;
    if (next_batch != nullptr) summary.metrics.packing_ns += next_batch->packing_ns;
    std::string pending_result_lines;
    pending_result_lines.reserve(
        static_cast<std::size_t>(config.checkpoint_every_candidates) * 1'024U);

    while (current_batch != nullptr) {
        await_prp_batch(*current_batch, summary);
        submit_independent_batch(*current_batch, config, independent_engine,
                                 summary.output_directory);
        submit_native_proof_batch(*current_batch, config, summary.output_directory, sha256,
                                  execution_options.native_proof_workers);
        await_independent_batch(*current_batch, summary, *flint_log);
        await_native_proof_batch(*current_batch, summary);
        if (next_batch != nullptr) {
            submit_prp_batch(*next_batch, prp_backend, summary);
        }
        std::size_t survivor_index = 0U;
        for (std::uint64_t index = current_batch->begin; index < current_batch->end; ++index) {
            SearchRecord record;
            record.campaign_id = summary.plan.campaign_id;
            record.candidate = candidate_at(config, index);
            record.work_unit_id = owner_for(summary.plan, index);
            record.status.novelty = NoveltyStatus::not_checked;
            const auto factor = sieve_result.factor_witnesses[static_cast<std::size_t>(index)];
            if (factor != 0U) {
                if (record.candidate.value <= factor || record.candidate.value % factor != 0U) {
                    throw std::logic_error("sieve returned an invalid proper-factor witness");
                }
                record.status.primality = PrimalityStatus::composite;
                record.status.verification = VerificationStatus::self_verified;
                record.classification_method = "CONGRUENCE_FACTOR";
                record.prp_status = "NOT_RUN";
                record.factor = factor;
                ++summary.sieve_composite_count;
                ++summary.composite_count;
            } else {
                if (survivor_index >= current_batch->survivor_offsets.size() ||
                    current_batch->survivor_offsets[survivor_index] !=
                        index - current_batch->begin) {
                    throw std::logic_error("PRP survivor ordering is inconsistent");
                }
                const auto prp_verdict = current_batch->verdicts[survivor_index++];
                ++summary.prp_tested_count;
                if (prp_verdict == prp::Base2StrongPrpVerdict::composite) {
                    record.status.primality = PrimalityStatus::composite;
                    record.status.verification = VerificationStatus::self_verified;
                    record.classification_method = "BASE2_STRONG_WITNESS";
                    record.prp_status = "FAILED";
                    ++summary.base2_composite_count;
                    ++summary.composite_count;
                } else if (prp_verdict == prp::Base2StrongPrpVerdict::probable_prime) {
                    record.status.primality = PrimalityStatus::probable_prime;
                    record.status.verification = VerificationStatus::unverified;
                    record.prp_status = "PASSED";
                    const auto decimal = std::to_string(record.candidate.value);
                    bool prime = false;
                    const auto &native =
                        current_batch->native_proof_results[survivor_index - 1U];
                    if (native.certificate.has_value()) {
                        const auto &evidence =
                            current_batch->native_proof_evidence[survivor_index - 1U];
                        if (!evidence.has_value()) {
                            throw std::logic_error(
                                "native Proth proof omitted its durable artifact");
                        }
                        record.native_proth_certificate = *evidence;
                        record.status.primality = PrimalityStatus::proven_prime;
                        record.status.verification = VerificationStatus::self_verified;
                        record.classification_method = "PROTH_CERTIFICATE_VALIDATED";
                        prime = true;
                    } else {
                        const ScopedTrace trace{"proof_fallback"};
                        const auto proof_started = Clock::now();
                        {
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
                            prime = primary.status.primality == PrimalityStatus::proven_prime;
                            record.primary_engine =
                                collect_evidence(proof_engine, primary, sha256, prime);
                            record.status.primality = primary.status.primality;
                            record.status.verification = prime ? VerificationStatus::self_verified
                                                               : VerificationStatus::unverified;
                            record.classification_method =
                                prime ? "PARI_PRIMECERT_VALIDATED" : "PARI_COMPOSITE";
                        }
                        summary.metrics.proof_ns += elapsed_ns(proof_started);
                    }

                    if (current_batch->independent_completed) {
                        const auto &independent =
                            current_batch->independent_results[survivor_index - 1U];
                        if (independent.status.primality != record.status.primality) {
                            throw std::runtime_error("independent engine disagrees at candidate " +
                                                     std::to_string(index));
                        }
                        const auto &slice = current_batch->independent_evidence_slices[
                            survivor_index - 1U];
                        if (!slice.has_value()) {
                            throw std::logic_error(
                                "independent batch omitted its FLINT journal slice");
                        }
                        record.independent_engine = collect_journal_evidence(
                            independent_engine, independent, summary.flint_evidence_path,
                            *slice);
                        committed_flint_evidence_bytes = slice->offset + slice->length;
                        record.status.verification = VerificationStatus::independently_verified;
                    } else {
                        const ScopedTrace trace{"verification"};
                        const auto verification_started = Clock::now();
                        const EngineRequest independent_request{
                            "flint-" + std::to_string(index), "primeforge.proth.uint64.v1", decimal,
                            summary.output_directory / "external" / "flint"};
                        if (!independent_engine.supports(independent_request)) {
                            throw std::runtime_error("configured independent engine does not "
                                                     "support the MVP family");
                        }
                        const auto independent = independent_engine.run(independent_request);
                        if (independent.status.primality != record.status.primality) {
                            throw std::runtime_error("independent engine disagrees at candidate " +
                                                     std::to_string(index));
                        }
                        const auto evidence_record =
                            make_flint_evidence_record(independent_request, independent);
                        std::vector<FlintEvidenceSlice> slices;
                        {
                            const ScopedTrace io_trace{"result_io"};
                            summary.metrics.io_ns += timed_action([&] {
                                slices = flint_log->append_batch(
                                    std::span{&evidence_record, 1U});
                            });
                        }
                        if (slices.size() != 1U) {
                            throw std::logic_error(
                                "FLINT journal omitted a single-run slice");
                        }
                        record.independent_engine = collect_journal_evidence(
                            independent_engine, independent, summary.flint_evidence_path,
                            slices.front());
                        committed_flint_evidence_bytes =
                            slices.front().offset + slices.front().length;
                        record.status.verification = VerificationStatus::independently_verified;
                        summary.metrics.verification_ns += elapsed_ns(verification_started);
                    }
                    ++summary.externally_classified_count;
                    if (prime)
                        ++summary.proven_prime_count;
                    else
                        ++summary.composite_count;
                } else {
                    throw std::logic_error("PRP backend returned an invalid verdict");
                }
            }

            const auto line = canonical_search_record(record, summary.output_directory) + "\n";
            pending_result_lines += line;
            summary.records.push_back(std::move(record));
            const auto next_index = index + 1U;
            const bool requested_stop =
                stop_now() || (execution_options.clean_stop_after_candidates.has_value() &&
                               next_index >= *execution_options.clean_stop_after_candidates);
            const bool checkpoint_due = requested_stop ||
                                        next_index == summary.plan.candidate_count ||
                                        next_index % config.checkpoint_every_candidates == 0U;
            if (checkpoint_due) {
                {
                    const ScopedTrace trace{"result_io"};
                    summary.metrics.io_ns += timed_action([&] {
                        append_durably(summary.results_path, pending_result_lines);
                        pending_result_lines.clear();
                    });
                }
                const ScopedTrace trace{"checkpoint"};
                summary.metrics.checkpoint_ns +=
                    timed_action([&] {
                        save_progress(summary, next_index, sha256, *flint_log,
                                      committed_flint_evidence_bytes);
                    });
            }
            if (requested_stop) {
                if (next_batch != nullptr) await_prp_batch(*next_batch, summary);
                summary.metrics.total_ns = elapsed_ns(total_started);
                return summary;
            }
        }
        if (survivor_index != current_batch->survivor_values.size()) {
            throw std::logic_error("PRP batch contains unconsumed survivors");
        }
        current_batch = std::move(next_batch);
        next_batch = current_batch != nullptr && current_batch->end < summary.plan.candidate_count
                         ? prepare_prp_batch(config, sieve_result, current_batch->end,
                                             bounded_batch_candidates)
                         : nullptr;
        if (next_batch != nullptr) summary.metrics.packing_ns += next_batch->packing_ns;
    }

    if (summary.proven_prime_count + summary.composite_count != summary.plan.candidate_count) {
        throw std::logic_error("search accounting is incomplete");
    }
    {
        const ScopedTrace trace{"result_io"};
        summary.metrics.io_ns += timed_action([&] { finalize_campaign(summary, sha256); });
    }
    summary.metrics.total_ns = elapsed_ns(total_started);
    return summary;
}

unsigned int select_sieve_threads(const std::uint64_t candidate_count,
                                  const unsigned int available_threads) noexcept {
    constexpr std::uint64_t candidates_per_worker = 1'024U;
    constexpr unsigned int target_physical_cores = 16U;
    if (available_threads == 0U || candidate_count < candidates_per_worker) return 1U;
    const auto work_limited = static_cast<unsigned int>(std::min<std::uint64_t>(
        target_physical_cores,
        std::max<std::uint64_t>(1U, candidate_count / candidates_per_worker)));
    return std::min(available_threads, work_limited);
}

std::string canonical_search_record(const SearchRecord &record,
                                    const std::filesystem::path &output_directory) {
    const auto decimal = [](const std::uint64_t value) {
        return quote_json(std::to_string(value));
    };
    return "{\"campaign_id\":" + quote_json(record.campaign_id) +
           ",\"classification_method\":" + quote_json(record.classification_method) +
           ",\"factor\":" +
           (record.factor.has_value() ? decimal(*record.factor) : std::string{"null"}) +
           ",\"flat_index\":" + decimal(record.candidate.flat_index) +
           ",\"independent_engine\":" + evidence_json(record.independent_engine, output_directory) +
           ",\"k\":" + decimal(record.candidate.k) + ",\"n\":" + decimal(record.candidate.n) +
           ",\"native_proth_certificate\":" +
           native_proof_json(record.native_proth_certificate, output_directory) +
           ",\"novelty_status\":" + quote_json(to_string(record.status.novelty)) +
           ",\"primality_status\":" + quote_json(to_string(record.status.primality)) +
           ",\"primary_engine\":" + evidence_json(record.primary_engine, output_directory) +
           ",\"prp_status\":" + quote_json(record.prp_status) +
           ",\"value\":" + decimal(record.candidate.value) +
           ",\"verification_status\":" + quote_json(to_string(record.status.verification)) +
           ",\"work_unit_id\":" + quote_json(record.work_unit_id) + "}";
}

}  // namespace primeforge::mvp
