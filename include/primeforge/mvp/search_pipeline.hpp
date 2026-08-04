// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"
#include "primeforge/core/status.hpp"
#include "primeforge/engine/engine_adapter.hpp"
#include "primeforge/mvp/search_config.hpp"
#include "primeforge/prp/base2_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace primeforge::mvp {

struct EngineEvidence {
    std::string engine_id;
    std::string executable_sha256;
    std::filesystem::path raw_stdout_path;
    std::filesystem::path raw_stderr_path;
    std::filesystem::path raw_log_path;
    std::optional<std::uint64_t> raw_log_offset;
    std::optional<std::uint64_t> raw_log_length;
    std::filesystem::path proof_artifact_path;
    std::string proof_artifact_sha256;
};

struct NativeProofEvidence {
    std::string format_version;
    std::filesystem::path artifact_path;
    std::string artifact_sha256;
    std::optional<std::uint64_t> artifact_offset;
    std::optional<std::uint64_t> artifact_length;
};

struct SearchRecord {
    std::string campaign_id;
    CandidateCoordinates candidate;
    CandidateStatus status;
    std::string work_unit_id;
    std::string classification_method;
    std::string prp_status;
    std::optional<std::uint64_t> factor;
    std::optional<NativeProofEvidence> native_proth_certificate;
    std::optional<EngineEvidence> primary_engine;
    std::optional<EngineEvidence> independent_engine;
};

struct SearchStageMetrics {
    std::uint32_t schema_version{2U};
    std::uint64_t generation_ns{};
    std::uint64_t congruence_ns{};
    std::uint64_t sieve_ns{};
    std::uint64_t packing_ns{};
    std::uint64_t host_to_device_ns{};
    std::uint64_t kernel_ns{};
    std::uint64_t device_to_host_ns{};
    std::uint64_t prp_cpu_ns{};
    std::uint64_t proof_ns{};
    std::uint64_t proof_compute_ns{};
    std::uint64_t proof_artifact_prepare_ns{};
    std::uint64_t proof_artifact_io_ns{};
    std::uint64_t verification_ns{};
    std::uint64_t io_ns{};
    std::uint64_t checkpoint_ns{};
    std::uint64_t prp_wait_ns{};
    std::uint64_t proof_wait_ns{};
    std::uint64_t verification_wait_ns{};
    std::uint64_t result_processing_ns{};
    std::uint64_t total_ns{};
};

struct PipelineTimelineEvent {
    std::string component;
    std::string name;
    std::string wait_reason;
    std::uint64_t start_ns{};
    std::uint64_t duration_ns{};
    std::uint64_t batch_begin{};
    std::uint64_t batch_end{};
};

struct SearchSummary {
    CampaignPlan plan;
    std::string compiled_table_sha256;
    std::string sieve_result_sha256;
    std::uint64_t sieve_composite_count{};
    unsigned int sieve_threads{};
    std::uint64_t base2_composite_count{};
    std::uint64_t prp_tested_count{};
    std::uint64_t prp_submitted_batches{};
    std::string prp_backend_id;
    std::size_t native_proof_workers{};
    std::uint64_t externally_classified_count{};
    std::uint64_t proven_prime_count{};
    std::uint64_t composite_count{};
    std::filesystem::path output_directory;
    std::filesystem::path results_path;
    std::filesystem::path flint_evidence_path;
    std::filesystem::path checkpoint_path;
    std::filesystem::path coverage_report_path;
    std::filesystem::path manifest_path;
    std::filesystem::path timeline_json_path;
    std::filesystem::path timeline_svg_path;
    bool completed{};
    SearchStageMetrics metrics;
    std::vector<PipelineTimelineEvent> timeline_events;
    std::vector<SearchRecord> records;
};

struct SearchExecutionOptions {
    bool resume_existing{};
    std::optional<std::uint64_t> clean_stop_after_candidates;
    std::function<bool()> stop_requested;
    prp::Base2StrongPrpBatchBackend* prp_backend{};
    std::size_t prp_batch_candidates{8'192U};
    std::size_t native_proof_workers{4U};
};

[[nodiscard]] SearchSummary execute_search(
    const SearchConfig& config,
    const Sha256Provider& sha256,
    EngineAdapter& proof_engine,
    EngineAdapter& independent_engine,
    const SearchExecutionOptions& options = {});

[[nodiscard]] std::string canonical_search_record(
    const SearchRecord& record, const std::filesystem::path& output_directory);

[[nodiscard]] unsigned int select_sieve_threads(
    std::uint64_t candidate_count, unsigned int available_threads) noexcept;

}  // namespace primeforge::mvp
