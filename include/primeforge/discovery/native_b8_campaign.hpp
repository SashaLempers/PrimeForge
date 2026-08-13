// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace primeforge::discovery::native_b8 {

struct Candidate {
    std::uint32_t k{};
    std::uint32_t n{};

    [[nodiscard]] friend constexpr bool operator==(const Candidate&, const Candidate&) = default;
};

struct CandidateResult {
    std::size_t lane_id{};
    Candidate candidate;
    std::string classification;
    std::uint32_t witness{};
    std::string res64;
    std::string validation_status{"GERBICZ_PASS"};
    std::string candidate_result_hash;
};

struct BatchPhaseTimings {
    std::optional<std::uint64_t> parameter_build_us;
    std::optional<std::uint64_t> host_to_device_us;
    std::optional<std::uint64_t> witness_selection_us;
    std::optional<std::uint64_t> a_pow_k_gpu_us;
    std::optional<std::uint64_t> main_ntt_gpu_us;
    std::optional<std::uint64_t> gerbicz_gpu_us;
    std::optional<std::uint64_t> final_reduce_gpu_us;
    std::optional<std::uint64_t> device_to_host_us;
    std::optional<std::uint64_t> worker_total_us;
};

struct BatchRequest {
    std::uint64_t batch_id{};
    std::uint64_t attempt_id{};
    std::vector<Candidate> candidates;
    std::string ordered_candidate_hash;
    std::string start_utc;
    std::filesystem::path input_path;
    std::filesystem::path stdout_path;
    std::filesystem::path stderr_path;
};

struct BatchExecution {
    std::vector<CandidateResult> results;
    BatchPhaseTimings phases;
    int worker_exit_code{};
    std::uint64_t result_parse_us{};
    bool stopped{};
    std::string error;
};

struct RuntimeProcessIds {
    std::uint64_t worker_pid{};
    std::uint64_t watchdog_pid{};
};

using TelemetryFields = std::map<std::string, std::string, std::less<>>;
using ResourceSink = std::function<void(const TelemetryFields&)>;
using RuntimeIdsSink = std::function<void(const RuntimeProcessIds&)>;
using StopRequested = std::function<bool()>;
using BatchExecutor = std::function<BatchExecution(
    const BatchRequest&, const ResourceSink&, const RuntimeIdsSink&, const StopRequested&)>;

struct TelemetryLimits {
    std::uint64_t soft_limit_bytes{80ULL * 1024ULL * 1024ULL};
    std::uint64_t hard_limit_bytes{90ULL * 1024ULL * 1024ULL};
    std::uint64_t normal_resource_interval_seconds{2U};
    std::uint64_t reduced_resource_interval_seconds{10U};
};

class TelemetryWriter {
public:
    TelemetryWriter(
        std::filesystem::path path,
        std::string campaign_id,
        TelemetryLimits limits = {});
    ~TelemetryWriter();
    TelemetryWriter(const TelemetryWriter&) = delete;
    TelemetryWriter& operator=(const TelemetryWriter&) = delete;

    bool append(
        std::string_view record_type,
        std::string_view scope,
        const TelemetryFields& fields = {},
        bool essential = false);
    void flush(bool durable = false);

    [[nodiscard]] std::uint64_t bytes_written() const noexcept;
    [[nodiscard]] std::size_t buffered_bytes() const noexcept;
    [[nodiscard]] std::uint64_t resource_interval_seconds() const noexcept;
    [[nodiscard]] bool resource_samples_enabled() const noexcept;
    [[nodiscard]] static bool valid_stream_prefix(const std::filesystem::path& path);

private:
    std::filesystem::path path_;
    std::string campaign_id_;
    TelemetryLimits limits_;
    std::string buffer_;
    std::uint64_t persisted_bytes_{};
    std::uint64_t last_flush_monotonic_ns_{};
    std::map<std::string, std::uint64_t, std::less<>> last_resource_sample_by_scope_;
    bool resource_samples_enabled_{true};
};

struct CampaignConfig {
    std::filesystem::path campaign_directory;
    std::filesystem::path remaining_queue_path;
    std::filesystem::path parent_completed_path;
    std::string campaign_id;
    std::string parent_campaign_id;
    std::string engine_commit;
    std::string engine_binary_sha256;
    std::string survivor_list_sha256;
    std::uint32_t batch_size{8U};
    std::uint64_t supervisor_pid{};
    TelemetryLimits telemetry_limits;
};

struct CampaignSummary {
    std::string state;
    std::size_t completed_from_parent{};
    std::size_t completed_this_resume{};
    std::size_t remaining{};
    std::uint64_t next_batch_id{};
    bool prime_found{};
    double throughput_candidates_per_hour{};
    std::string campaign_results_hash;
};

[[nodiscard]] std::vector<Candidate> read_candidate_file(const std::filesystem::path& path);
[[nodiscard]] std::string candidate_set_sha256(
    const std::vector<Candidate>& candidates,
    const Sha256Provider& sha256);
[[nodiscard]] std::string ordered_batch_sha256(
    const std::vector<Candidate>& candidates,
    const Sha256Provider& sha256);
[[nodiscard]] BatchExecution parse_worker_output(
    std::string_view output,
    const std::vector<Candidate>& expected,
    int worker_exit_code);
[[nodiscard]] CampaignSummary run_campaign(
    const CampaignConfig& config,
    const BatchExecutor& executor,
    const Sha256Provider& sha256,
    StopRequested stop_requested = {});

}  // namespace primeforge::discovery::native_b8
