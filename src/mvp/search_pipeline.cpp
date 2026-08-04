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
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <future>
#include <iterator>
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

[[nodiscard]] std::uint64_t offset_ns(const Clock::time_point origin,
                                      const Clock::time_point point) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(point - origin).count());
}

void add_timeline_event(SearchSummary &summary, const Clock::time_point origin,
                        const std::string_view component, const std::string_view name,
                        const Clock::time_point started, const Clock::time_point finished,
                        const std::uint64_t batch_begin = 0U,
                        const std::uint64_t batch_end = 0U,
                        const std::string_view wait_reason = {}) {
    summary.timeline_events.push_back(PipelineTimelineEvent{
        std::string{component}, std::string{name}, std::string{wait_reason},
        offset_ns(origin, started),
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      finished - started)
                                      .count()),
        batch_begin, batch_end});
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
    return "{\"artifact_length\":" +
           (evidence->artifact_length.has_value()
                ? quote_json(std::to_string(*evidence->artifact_length))
                : std::string{"null"}) +
           ",\"artifact_offset\":" +
           (evidence->artifact_offset.has_value()
                ? quote_json(std::to_string(*evidence->artifact_offset))
                : std::string{"null"}) +
           ",\"artifact_path\":" +
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
    const bool has_stdout_path = !result.raw_stdout_path.empty();
    const bool has_stderr_path = !result.raw_stderr_path.empty();
    if (has_inline_stdout != has_inline_stderr) {
        throw std::runtime_error("independent engine returned incomplete inline raw output");
    }
    if (has_stdout_path != has_stderr_path) {
        throw std::runtime_error("independent engine returned incomplete raw-output paths");
    }
    const bool has_inline_pair = has_inline_stdout && has_inline_stderr;
    const bool has_path_pair = has_stdout_path && has_stderr_path;
    if (has_inline_pair == has_path_pair) {
        throw std::runtime_error(
            "independent engine must return exactly one raw-output representation");
    }
    if (has_inline_pair) {
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
    const bool written = content.empty() ||
                         std::fwrite(content.data(), 1U, content.size(), file) == content.size();
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
    const auto configuration_digest = sha256_from_hex(result.configuration_sha256);
    const auto evidence_digest = sha256_from_hex(result.flint_evidence_sha256);
    const auto results_digest = sha256_from_hex(result.results_sha256);
    if (!configuration_digest.has_value() || !evidence_digest.has_value() ||
        !results_digest.has_value()) {
        throw std::runtime_error("checkpoint payload contains invalid SHA-256");
    }
    if (sha256_to_hex(*configuration_digest) != result.configuration_sha256 ||
        sha256_to_hex(*evidence_digest) != result.flint_evidence_sha256 ||
        sha256_to_hex(*results_digest) != result.results_sha256) {
        throw std::runtime_error("checkpoint payload contains noncanonical SHA-256");
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
    Clock::time_point packing_started{};
    Clock::time_point prp_started{};
    Clock::time_point prp_finished{};
    Clock::time_point independent_task_started{};
    Clock::time_point independent_task_finished{};
    Clock::time_point proof_task_started{};
    Clock::time_point proof_task_finished{};
    std::future<prp::Base2StrongPrpBatchMetrics> completion;
    std::future<std::vector<EngineResult>> independent_completion;
    std::vector<std::size_t> independent_survivors;
    std::vector<EngineRequest> independent_requests;
    std::vector<EngineResult> independent_results;
    std::vector<std::optional<FlintEvidenceSlice>> independent_evidence_slices;
    struct NativeProofBatch {
        std::vector<proth::ProofAttempt> results;
        std::vector<std::optional<NativeProofEvidence>> evidence;
        std::vector<std::optional<std::string>> certificate_bytes;
        std::uint64_t elapsed_ns{};
        std::uint64_t compute_ns{};
        std::uint64_t artifact_prepare_ns{};
        std::uint64_t artifact_io_ns{};
    };
    std::future<NativeProofBatch> native_proof_completion;
    std::vector<proth::ProofAttempt> native_proof_results;
    std::vector<std::optional<NativeProofEvidence>> native_proof_evidence;
    bool independent_submitted{};
    bool independent_completed{};
};

[[nodiscard]] std::unique_ptr<PreparedPrpBatch>
prepare_prp_batch(const SearchConfig &config, const family_sieve::Result &sieve_result,
                  const std::uint64_t begin, const std::uint64_t batch_candidates) {
    const auto packing_started = Clock::now();
    const ScopedTrace trace{"candidate_pack"};
    auto batch = std::make_unique<PreparedPrpBatch>();
    batch->packing_started = packing_started;
    batch->begin = begin;
    const auto total = candidate_count(config);
    const auto checkpoint_remainder =
        config.checkpoint_every_candidates - begin % config.checkpoint_every_candidates;
    batch->end = begin +
                 std::min({batch_candidates, total - begin, checkpoint_remainder});
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
    auto *const started = &batch.prp_started;
    auto *const finished = &batch.prp_finished;
    batch.completion = std::async(std::launch::async, [values, verdicts, selected_backend,
                                                       started, finished] {
        const ScopedTrace trace{"prp_batch"};
        *started = Clock::now();
        auto metrics = selected_backend->test(*values, *verdicts);
        *finished = Clock::now();
        return metrics;
    });
}

void await_prp_batch(PreparedPrpBatch &batch, SearchSummary &summary,
                     const Clock::time_point timeline_origin,
                     std::optional<Clock::time_point> &previous_prp_finished) {
    if (!batch.completion.valid()) return;
    const auto wait_started = Clock::now();
    const auto metrics = batch.completion.get();
    const auto wait_finished = Clock::now();
    summary.metrics.prp_wait_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(wait_finished - wait_started)
            .count());
    add_timeline_event(summary, timeline_origin, "CPU_COORDINATOR", "wait_for_prp",
                       wait_started, wait_finished, batch.begin, batch.end,
                       "GPU_OR_CPU_PRP_NOT_FINISHED");
    if (previous_prp_finished.has_value() && batch.prp_started > *previous_prp_finished) {
        add_timeline_event(summary, timeline_origin, "GPU_IDLE", "waiting_for_next_batch",
                           *previous_prp_finished, batch.prp_started, batch.begin, batch.end,
                           "NO_READY_PRP_BATCH_SUBMITTED");
    }
    const std::string_view prp_component =
        metrics.used_accelerator ? "GPU_PRP" : "CPU_PRP";
    add_timeline_event(summary, timeline_origin, prp_component, "prp_total",
                       batch.prp_started, batch.prp_finished, batch.begin, batch.end);
    auto part_started = batch.prp_started;
    const auto add_gpu_part = [&](const std::string_view name, const std::uint64_t duration) {
        const auto part_finished = part_started + std::chrono::nanoseconds{duration};
        add_timeline_event(summary, timeline_origin, prp_component, name, part_started,
                           part_finished, batch.begin, batch.end);
        part_started = part_finished;
    };
    add_gpu_part("h2d_copy", metrics.host_to_device_ns);
    add_gpu_part("kernel", metrics.kernel_ns);
    add_gpu_part("d2h_copy", metrics.device_to_host_ns);
    previous_prp_finished = batch.prp_finished;
    summary.metrics.prp_cpu_ns += metrics.cpu_ns;
    summary.metrics.host_to_device_ns += metrics.host_to_device_ns;
    summary.metrics.kernel_ns += metrics.kernel_ns;
    summary.metrics.device_to_host_ns += metrics.device_to_host_ns;
}

void submit_independent_batch(PreparedPrpBatch &batch, const SearchConfig &config,
                               EngineAdapter &independent_engine,
                               const std::filesystem::path &output_directory) {
    if (batch.survivor_values.empty()) return;
    batch.independent_results.resize(batch.verdicts.size());
    batch.independent_evidence_slices.resize(batch.verdicts.size());
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
    auto *const task_started = &batch.independent_task_started;
    auto *const task_finished = &batch.independent_task_finished;
    batch.independent_completion = std::async(
        std::launch::async,
        [engine, requests = batch.independent_requests, task_started,
         task_finished]() mutable {
            const ScopedTrace trace{"verification"};
            *task_started = Clock::now();
            auto results = engine->run_batch(requests);
            *task_finished = Clock::now();
            return results;
        });
}

void await_independent_batch(PreparedPrpBatch &batch, SearchSummary &summary,
                             FlintEvidenceLog &flint_log,
                             const Clock::time_point timeline_origin) {
    if (!batch.independent_submitted) return;
    const auto wait_started = Clock::now();
    auto results = batch.independent_completion.get();
    const auto wait_finished = Clock::now();
    const auto wait_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(wait_finished - wait_started)
            .count());
    summary.metrics.verification_wait_ns += wait_ns;
    add_timeline_event(summary, timeline_origin, "CPU_COORDINATOR", "wait_for_flint",
                       wait_started, wait_finished, batch.begin, batch.end,
                       "FLINT_BATCH_NOT_FINISHED");
    add_timeline_event(summary, timeline_origin, "FLINT", "independent_verification",
                       batch.independent_task_started, batch.independent_task_finished,
                       batch.begin, batch.end);
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
        const auto io_started = Clock::now();
        const ScopedTrace trace{"result_io"};
        summary.metrics.io_ns += timed_action(
            [&] { slices = flint_log.append_batch(evidence_records); });
        add_timeline_event(summary, timeline_origin, "IO_CHECKPOINT",
                           "write_flint_evidence", io_started, Clock::now(), batch.begin,
                           batch.end);
    }
    if (slices.size() != results.size()) {
        throw std::logic_error("FLINT journal returned the wrong slice count");
    }
    for (std::size_t index = 0U; index < results.size(); ++index) {
        const auto survivor = batch.independent_survivors[index];
        batch.independent_results[survivor] = std::move(results[index]);
        batch.independent_evidence_slices[survivor] = slices[index];
    }
    summary.metrics.verification_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            batch.independent_task_finished - batch.independent_task_started)
            .count());
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
            batch.proof_task_started = started;
            PreparedPrpBatch::NativeProofBatch completed;
            completed.results.resize(batch.verdicts.size());
            completed.evidence.resize(batch.verdicts.size());
            completed.certificate_bytes.resize(batch.verdicts.size());
            std::vector<std::size_t> probable_survivors;
            probable_survivors.reserve(batch.verdicts.size());
            for (std::size_t survivor = 0U; survivor < batch.verdicts.size(); ++survivor) {
                if (batch.verdicts[survivor] ==
                    prp::Base2StrongPrpVerdict::probable_prime) {
                    probable_survivors.push_back(survivor);
                }
            }

            const auto proof_directory = output_directory / "proofs" / "proth";
            std::filesystem::create_directories(proof_directory);
            const auto worker_count =
                std::min(requested_worker_count, probable_survivors.size());
            std::atomic_size_t next_survivor{0U};
            std::atomic_bool stop_workers{false};
            std::atomic_uint64_t compute_ns{0U};
            std::atomic_uint64_t artifact_prepare_ns{0U};
            std::atomic_uint64_t artifact_io_ns{0U};
            std::mutex certificate_mutex;
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
                        const auto compute_started = Clock::now();
                        auto attempt = proth::try_prove_u64(
                            candidate.k, static_cast<std::uint32_t>(candidate.n), 65'535U);
                        compute_ns.fetch_add(elapsed_ns(compute_started),
                                             std::memory_order_relaxed);
                        if (attempt.certificate.has_value()) {
                            const auto prepare_started = Clock::now();
                            const auto certificate_bytes =
                                proth::canonical_certificate(*attempt.certificate);
                            {
                                const std::scoped_lock lock{certificate_mutex};
                                completed.certificate_bytes[survivor] = certificate_bytes;
                            }
                            artifact_prepare_ns.fetch_add(elapsed_ns(prepare_started),
                                                          std::memory_order_relaxed);
                        }
                        completed.results[survivor] = std::move(attempt);
                    }
                } catch (...) {
                    {
                        const std::scoped_lock lock{failure_mutex};
                        if (first_failure == nullptr)
                            first_failure = std::current_exception();
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
            std::string certificate_journal;
            const auto segment_begin =
                batch.begin - batch.begin % config.checkpoint_every_candidates;
            const auto segment_end = std::min(
                candidate_count(config), segment_begin + config.checkpoint_every_candidates);
            const auto journal_path =
                proof_directory /
                ("segment-" + std::to_string(segment_begin) + "-" +
                 std::to_string(segment_end) + ".jsonl");
            const auto journal_base_offset =
                std::filesystem::is_regular_file(journal_path)
                    ? static_cast<std::uint64_t>(std::filesystem::file_size(journal_path))
                    : 0U;
            const auto journal_prepare_started = Clock::now();
            for (const auto survivor : probable_survivors) {
                if (!completed.certificate_bytes[survivor].has_value()) continue;
                const auto &certificate_bytes = *completed.certificate_bytes[survivor];
                const auto offset = journal_base_offset +
                                    static_cast<std::uint64_t>(certificate_journal.size());
                const auto length = static_cast<std::uint64_t>(certificate_bytes.size());
                certificate_journal += certificate_bytes;
                certificate_journal.push_back('\n');
                completed.evidence[survivor] = NativeProofEvidence{
                    proth::certificate_format, journal_path,
                    hash_text(certificate_bytes, sha256), offset, length};
            }
            artifact_prepare_ns.fetch_add(elapsed_ns(journal_prepare_started),
                                          std::memory_order_relaxed);
            if (!certificate_journal.empty()) {
                const auto io_started = Clock::now();
                append_durably(journal_path, certificate_journal);
                artifact_io_ns.fetch_add(elapsed_ns(io_started),
                                         std::memory_order_relaxed);
            }
            completed.elapsed_ns = elapsed_ns(started);
            completed.compute_ns = compute_ns.load(std::memory_order_relaxed);
            completed.artifact_prepare_ns =
                artifact_prepare_ns.load(std::memory_order_relaxed);
            completed.artifact_io_ns = artifact_io_ns.load(std::memory_order_relaxed);
            batch.proof_task_finished = Clock::now();
            return completed;
        });
}

void await_native_proof_batch(PreparedPrpBatch &batch, SearchSummary &summary,
                              const Clock::time_point timeline_origin) {
    const auto wait_started = Clock::now();
    auto completed = batch.native_proof_completion.get();
    const auto wait_finished = Clock::now();
    summary.metrics.proof_wait_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(wait_finished - wait_started)
            .count());
    add_timeline_event(summary, timeline_origin, "CPU_COORDINATOR", "wait_for_proth_proof",
                       wait_started, wait_finished, batch.begin, batch.end,
                       "CPU_PROTH_WORKERS_NOT_FINISHED");
    add_timeline_event(summary, timeline_origin, "CPU_PROTH", "proth_proof",
                       batch.proof_task_started, batch.proof_task_finished, batch.begin,
                       batch.end);
    batch.native_proof_results = std::move(completed.results);
    batch.native_proof_evidence = std::move(completed.evidence);
    summary.metrics.proof_ns += completed.elapsed_ns;
    summary.metrics.proof_compute_ns += completed.compute_ns;
    summary.metrics.proof_artifact_prepare_ns += completed.artifact_prepare_ns;
    summary.metrics.proof_artifact_io_ns += completed.artifact_io_ns;
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
    const auto partial_segment_begin =
        state.sequence - state.sequence % config.checkpoint_every_candidates;
    const bool has_partial_segment =
        state.sequence % config.checkpoint_every_candidates != 0U;
    const auto partial_segment_end = std::min(
        summary.plan.candidate_count,
        partial_segment_begin + config.checkpoint_every_candidates);
    const auto partial_segment_marker =
        "\"artifact_path\":\"proofs/proth/segment-" +
        std::to_string(partial_segment_begin) + "-" +
        std::to_string(partial_segment_end) + ".jsonl\"";
    std::uint64_t partial_segment_certificates = 0U;
    std::size_t offset = 0U;
    for (std::uint64_t index = 0U; index < state.sequence; ++index) {
        const auto end = prefix.find('\n', offset);
        const auto line = prefix.substr(offset, end - offset);
        account_existing_line(summary, line);
        if (has_partial_segment && index >= partial_segment_begin &&
            line.find(partial_segment_marker) != std::string_view::npos) {
            ++partial_segment_certificates;
        }
        offset = end + 1U;
    }
    const auto proof_directory = summary.output_directory / "proofs" / "proth";
    if (std::filesystem::is_directory(proof_directory)) {
        for (const auto &entry : std::filesystem::directory_iterator(proof_directory)) {
            if (!entry.is_regular_file()) continue;
            const auto name = entry.path().filename().string();
            if (!name.starts_with("segment-") || !name.ends_with(".jsonl")) continue;
            const std::string_view stem{name.data() + 8U, name.size() - 8U - 6U};
            const auto separator = stem.find('-');
            if (separator == std::string_view::npos) {
                throw std::runtime_error("malformed Proth certificate segment name");
            }
            const auto begin = parse_decimal(stem.substr(0U, separator),
                                             "Proth certificate segment begin");
            const auto end = parse_decimal(stem.substr(separator + 1U),
                                           "Proth certificate segment end");
            if (begin >= end || end > summary.plan.candidate_count) {
                throw std::runtime_error("invalid Proth certificate segment interval");
            }
            if (begin >= state.sequence) {
                std::filesystem::remove(entry.path());
                continue;
            }
            if (!has_partial_segment || begin != partial_segment_begin ||
                end <= state.sequence) {
                continue;
            }
            const auto journal = read_file(entry.path());
            std::size_t authenticated_bytes = 0U;
            for (std::uint64_t certificate = 0U;
                 certificate < partial_segment_certificates; ++certificate) {
                const auto newline = journal.find('\n', authenticated_bytes);
                if (newline == std::string::npos) {
                    throw std::runtime_error(
                        "Proth certificate segment is shorter than the checkpoint prefix");
                }
                authenticated_bytes = newline + 1U;
            }
            if (journal.size() != authenticated_bytes) {
                if (authenticated_bytes == 0U) {
                    std::filesystem::remove(entry.path());
                } else {
                    write_atomic(entry.path(),
                                 std::string_view{journal}.substr(0U, authenticated_bytes));
                }
            }
        }
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

void write_timeline_artifacts(SearchSummary &summary) {
    std::filesystem::create_directories(summary.timeline_json_path.parent_path());
    std::ranges::sort(summary.timeline_events, [](const auto &left, const auto &right) {
        if (left.start_ns != right.start_ns) return left.start_ns < right.start_ns;
        if (left.component != right.component) return left.component < right.component;
        return left.name < right.name;
    });

    const auto decimal = [](const std::uint64_t value) { return std::to_string(value); };
    const auto component_id = [](const std::string_view component) -> std::uint64_t {
        if (component == "CPU_SETUP") return 1U;
        if (component == "CPU_COORDINATOR") return 2U;
        if (component == "CPU_PRP") return 3U;
        if (component == "GPU_PRP") return 4U;
        if (component == "GPU_IDLE") return 5U;
        if (component == "CPU_PROTH") return 6U;
        if (component == "FLINT") return 7U;
        if (component == "IO_CHECKPOINT") return 8U;
        return 9U;
    };
    std::string trace{"{\"displayTimeUnit\":\"ns\",\"traceEvents\":["};
    bool first = true;
    for (const auto &event : summary.timeline_events) {
        if (!first) trace.push_back(',');
        first = false;
        trace += "{\"args\":{\"batch_begin\":" + quote_json(decimal(event.batch_begin)) +
                 ",\"batch_end\":" + quote_json(decimal(event.batch_end)) +
                 ",\"wait_reason\":" + quote_json(event.wait_reason) +
                 "},\"cat\":" + quote_json(event.component) + ",\"dur\":" +
                 decimal(event.duration_ns / 1'000U) + ",\"name\":" +
                 quote_json(event.name) +
                 ",\"ph\":\"X\",\"pid\":1,\"tid\":" +
                 decimal(component_id(event.component)) + ",\"ts\":" +
                 decimal(event.start_ns / 1'000U) + "}";
    }
    trace += "]}";
    write_atomic(summary.timeline_json_path, trace);

    constexpr std::array<std::string_view, 8U> lanes{
        "CPU_SETUP", "CPU_COORDINATOR", "CPU_PRP", "GPU_PRP", "GPU_IDLE",
        "CPU_PROTH", "FLINT", "IO_CHECKPOINT"};
    constexpr std::uint64_t canvas_width = 1'800U;
    constexpr std::uint64_t label_width = 180U;
    constexpr std::uint64_t lane_height = 64U;
    std::uint64_t timeline_end = 1U;
    for (const auto &event : summary.timeline_events) {
        timeline_end = std::max(timeline_end, event.start_ns + event.duration_ns);
    }
    const auto lane_index = [&](const std::string_view component) {
        for (std::size_t index = 0U; index < lanes.size(); ++index) {
            if (lanes[index] == component) return index;
        }
        return std::size_t{1U};
    };
    const auto colour = [](const std::string_view component) {
        if (component == "GPU_PRP") return std::string_view{"#37b24d"};
        if (component == "GPU_IDLE") return std::string_view{"#ff6b6b"};
        if (component == "CPU_PROTH") return std::string_view{"#1c7ed6"};
        if (component == "FLINT") return std::string_view{"#7950f2"};
        if (component == "IO_CHECKPOINT") return std::string_view{"#f08c00"};
        if (component == "CPU_COORDINATOR") return std::string_view{"#15aabf"};
        return std::string_view{"#868e96"};
    };
    const auto canvas_height = 48U + lanes.size() * lane_height;
    std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" +
        decimal(canvas_width) + "\" height=\"" + decimal(canvas_height) +
        "\" viewBox=\"0 0 " + decimal(canvas_width) + " " +
        decimal(canvas_height) + "\"><style>text{font-family:Segoe UI,Arial,sans-serif;"
        "font-size:13px}.lane{fill:#f8f9fa;stroke:#dee2e6}.bar{opacity:.82}</style>"
        "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>"
        "<text x=\"12\" y=\"24\" font-size=\"17\" font-weight=\"600\">"
        "PrimeForge pipeline timeline</text>";
    for (std::size_t index = 0U; index < lanes.size(); ++index) {
        const auto y = 40U + index * lane_height;
        svg += "<rect class=\"lane\" x=\"0\" y=\"" + decimal(y) +
               "\" width=\"" + decimal(canvas_width) + "\" height=\"" +
               decimal(lane_height) + "\"/><text x=\"10\" y=\"" +
               decimal(y + 36U) + "\">" + std::string{lanes[index]} + "</text>";
    }
    for (const auto &event : summary.timeline_events) {
        const auto lane = lane_index(event.component);
        const auto x = label_width +
                       event.start_ns * (canvas_width - label_width) / timeline_end;
        const auto width = std::max<std::uint64_t>(
            1U, event.duration_ns * (canvas_width - label_width) / timeline_end);
        const auto y = 48U + lane * lane_height;
        svg += "<rect class=\"bar\" x=\"" + decimal(x) + "\" y=\"" +
               decimal(y) + "\" width=\"" + decimal(width) +
               "\" height=\"48\" fill=\"" + std::string{colour(event.component)} +
               "\"><title>" + event.component + ": " + event.name;
        if (!event.wait_reason.empty()) svg += " - " + event.wait_reason;
        svg += "</title></rect>";
    }
    svg += "</svg>";
    write_atomic(summary.timeline_svg_path, svg);
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
        const auto stage_started = Clock::now();
        const ScopedTrace trace{"candidate_generation"};
        auto [duration, plan] = timed_value([&] { return build_campaign_plan(config, sha256); });
        summary.metrics.generation_ns += duration;
        summary.plan = std::move(plan);
        add_timeline_event(summary, total_started, "CPU_SETUP", "candidate_generation",
                           stage_started, Clock::now());
    }
    summary.output_directory = std::filesystem::absolute(config.output_directory);
    summary.results_path = summary.output_directory / "results.jsonl";
    summary.flint_evidence_path =
        summary.output_directory / "external" / "flint" / "evidence.jsonl";
    summary.checkpoint_path = summary.output_directory / "campaign.checkpoint.json";
    summary.coverage_report_path = summary.output_directory / "coverage_report.json";
    summary.manifest_path = summary.output_directory / "MANIFEST.sha256";
    const auto telemetry_directory = summary.output_directory.parent_path() / "telemetry";
    const auto campaign_leaf = summary.output_directory.filename().string();
    summary.timeline_json_path =
        telemetry_directory / (campaign_leaf + ".pipeline_timeline.json");
    summary.timeline_svg_path =
        telemetry_directory / (campaign_leaf + ".pipeline_timeline.svg");
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
        flint_log = std::make_unique<FlintEvidenceLog>(
            summary.flint_evidence_path, sha256,
            FlintEvidenceLog::OpenMode::open_existing);
        {
            const auto stage_started = Clock::now();
            const ScopedTrace trace{"checkpoint"};
            const auto [duration, restored] =
                timed_value([&] { return restore_progress(summary, config, sha256, *flint_log); });
            summary.metrics.checkpoint_ns += duration;
            first_index = restored.next_index;
            committed_flint_evidence_bytes = restored.flint_evidence_bytes;
            add_timeline_event(summary, total_started, "IO_CHECKPOINT",
                               "restore_checkpoint", stage_started, Clock::now(),
                               first_index, first_index);
        }
    } else {
        if (std::filesystem::exists(summary.output_directory)) {
            throw std::invalid_argument("campaign output directory already exists; use resume");
        }
        std::filesystem::create_directories(summary.output_directory);
        {
            const auto stage_started = Clock::now();
            const ScopedTrace trace{"result_io"};
            summary.metrics.io_ns += timed_action([&] {
                write_atomic(summary.output_directory / "search.yaml",
                             render_search_config_yaml(config));
                std::filesystem::create_directories(summary.flint_evidence_path.parent_path());
                flint_log = std::make_unique<FlintEvidenceLog>(
                    summary.flint_evidence_path, sha256,
                    FlintEvidenceLog::OpenMode::create_if_missing);
                append_durably(summary.results_path, {});
            });
            add_timeline_event(summary, total_started, "IO_CHECKPOINT",
                               "initialize_campaign_files", stage_started, Clock::now());
        }
        {
            const auto stage_started = Clock::now();
            const ScopedTrace trace{"checkpoint"};
            summary.metrics.checkpoint_ns +=
                timed_action([&] {
                    save_progress(summary, 0U, sha256, *flint_log, 0U);
                });
            add_timeline_event(summary, total_started, "IO_CHECKPOINT",
                               "write_checkpoint", stage_started, Clock::now());
        }
    }

    congruence::CompiledTable table;
    {
        const auto stage_started = Clock::now();
        const ScopedTrace trace{"congruence_compile"};
        auto [duration, compiled] = timed_value([&] {
            const auto primes =
                sieve::generate_primes_reference(2U, config.sieve_maximum_prime + 1U).primes;
            return congruence::compile_congruences(
                make_affine_family(config), primes, {}, sha256);
        });
        summary.metrics.congruence_ns += duration;
        table = std::move(compiled);
        add_timeline_event(summary, total_started, "CPU_SETUP", "congruence_compile",
                           stage_started, Clock::now());
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
        const auto stage_started = Clock::now();
        const ScopedTrace trace{"sieve"};
        auto [duration, result] =
            timed_value([&] { return family_sieve::run(table, sha256, options); });
        summary.metrics.sieve_ns += duration;
        sieve_result = std::move(result);
        add_timeline_event(summary, total_started, "CPU_SETUP", "family_sieve",
                           stage_started, Clock::now());
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
    add_timeline_event(summary, total_started, "CPU_COORDINATOR", "prepare_batch",
                       current_batch->packing_started,
                       current_batch->packing_started +
                           std::chrono::nanoseconds{current_batch->packing_ns},
                       current_batch->begin, current_batch->end);
    submit_prp_batch(*current_batch, prp_backend, summary);
    auto next_batch =
        current_batch->end < summary.plan.candidate_count
            ? prepare_prp_batch(config, sieve_result, current_batch->end, bounded_batch_candidates)
            : nullptr;
    if (next_batch != nullptr) {
        summary.metrics.packing_ns += next_batch->packing_ns;
        add_timeline_event(summary, total_started, "CPU_COORDINATOR", "prepare_batch",
                           next_batch->packing_started,
                           next_batch->packing_started +
                               std::chrono::nanoseconds{next_batch->packing_ns},
                           next_batch->begin, next_batch->end);
    }
    std::string pending_result_lines;
    pending_result_lines.reserve(
        static_cast<std::size_t>(config.checkpoint_every_candidates) * 1'024U);

    std::optional<Clock::time_point> previous_prp_finished;
    while (current_batch != nullptr) {
        await_prp_batch(*current_batch, summary, total_started, previous_prp_finished);
        if (next_batch != nullptr) {
            submit_prp_batch(*next_batch, prp_backend, summary);
        }
        submit_independent_batch(*current_batch, config, independent_engine,
                                 summary.output_directory);
        submit_native_proof_batch(*current_batch, config, summary.output_directory, sha256,
                                  execution_options.native_proof_workers);
        await_independent_batch(*current_batch, summary, *flint_log, total_started);
        await_native_proof_batch(*current_batch, summary, total_started);
        const auto result_processing_started = Clock::now();
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
                    const auto io_started = Clock::now();
                    const ScopedTrace trace{"result_io"};
                    summary.metrics.io_ns += timed_action([&] {
                        append_durably(summary.results_path, pending_result_lines);
                        pending_result_lines.clear();
                    });
                    add_timeline_event(summary, total_started, "IO_CHECKPOINT",
                                       "write_results", io_started, Clock::now(),
                                       current_batch->begin, next_index);
                }
                const auto checkpoint_started = Clock::now();
                const ScopedTrace trace{"checkpoint"};
                summary.metrics.checkpoint_ns +=
                    timed_action([&] {
                        save_progress(summary, next_index, sha256, *flint_log,
                                      committed_flint_evidence_bytes);
                    });
                add_timeline_event(summary, total_started, "IO_CHECKPOINT",
                                   "write_checkpoint", checkpoint_started, Clock::now(),
                                   current_batch->begin, next_index);
            }
            if (requested_stop) {
                const auto result_processing_finished = Clock::now();
                const auto result_processing_ns = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        result_processing_finished - result_processing_started)
                        .count());
                summary.metrics.result_processing_ns += result_processing_ns;
                add_timeline_event(summary, total_started, "CPU_COORDINATOR",
                                   "result_processing", result_processing_started,
                                   result_processing_finished, current_batch->begin,
                                   next_index);
                if (next_batch != nullptr)
                    await_prp_batch(*next_batch, summary, total_started,
                                    previous_prp_finished);
                summary.metrics.total_ns = elapsed_ns(total_started);
                return summary;
            }
        }
        const auto result_processing_finished = Clock::now();
        const auto result_processing_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                result_processing_finished - result_processing_started)
                .count());
        summary.metrics.result_processing_ns += result_processing_ns;
        add_timeline_event(summary, total_started, "CPU_COORDINATOR",
                           "result_processing", result_processing_started,
                           result_processing_finished, current_batch->begin,
                           current_batch->end);
        if (survivor_index != current_batch->survivor_values.size()) {
            throw std::logic_error("PRP batch contains unconsumed survivors");
        }
        current_batch = std::move(next_batch);
        next_batch = current_batch != nullptr && current_batch->end < summary.plan.candidate_count
                         ? prepare_prp_batch(config, sieve_result, current_batch->end,
                                             bounded_batch_candidates)
                         : nullptr;
        if (next_batch != nullptr) {
            summary.metrics.packing_ns += next_batch->packing_ns;
            add_timeline_event(summary, total_started, "CPU_COORDINATOR", "prepare_batch",
                               next_batch->packing_started,
                               next_batch->packing_started +
                                   std::chrono::nanoseconds{next_batch->packing_ns},
                               next_batch->begin, next_batch->end);
        }
    }

    if (summary.proven_prime_count + summary.composite_count != summary.plan.candidate_count) {
        throw std::logic_error("search accounting is incomplete");
    }
    summary.metrics.total_ns = elapsed_ns(total_started);
    {
        const ScopedTrace trace{"result_io"};
        summary.metrics.io_ns += timed_action([&] { write_timeline_artifacts(summary); });
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
