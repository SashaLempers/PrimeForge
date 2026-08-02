// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"
#include "primeforge/core/status.hpp"
#include "primeforge/engine/engine_adapter.hpp"
#include "primeforge/mvp/search_config.hpp"

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
    std::filesystem::path proof_artifact_path;
    std::string proof_artifact_sha256;
};

struct SearchRecord {
    std::string campaign_id;
    CandidateCoordinates candidate;
    CandidateStatus status;
    std::string work_unit_id;
    std::string classification_method;
    std::string prp_status;
    std::optional<std::uint64_t> factor;
    std::optional<EngineEvidence> primary_engine;
    std::optional<EngineEvidence> independent_engine;
};

struct SearchSummary {
    CampaignPlan plan;
    std::string compiled_table_sha256;
    std::string sieve_result_sha256;
    std::uint64_t sieve_composite_count{};
    std::uint64_t base2_composite_count{};
    std::uint64_t externally_classified_count{};
    std::uint64_t proven_prime_count{};
    std::uint64_t composite_count{};
    std::filesystem::path output_directory;
    std::filesystem::path results_path;
    std::filesystem::path checkpoint_path;
    std::filesystem::path coverage_report_path;
    std::filesystem::path manifest_path;
    bool completed{};
    std::vector<SearchRecord> records;
};

struct SearchExecutionOptions {
    bool resume_existing{};
    std::optional<std::uint64_t> clean_stop_after_candidates;
    std::function<bool()> stop_requested;
};

[[nodiscard]] SearchSummary execute_search(
    const SearchConfig& config,
    const Sha256Provider& sha256,
    EngineAdapter& proof_engine,
    EngineAdapter& independent_engine,
    const SearchExecutionOptions& options = {});

[[nodiscard]] std::string canonical_search_record(
    const SearchRecord& record, const std::filesystem::path& output_directory);

}  // namespace primeforge::mvp
