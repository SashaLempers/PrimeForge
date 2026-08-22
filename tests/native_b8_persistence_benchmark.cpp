// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/discovery/native_b8_campaign.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace native = primeforge::discovery::native_b8;
using Clock = std::chrono::steady_clock;

void write_candidates(
    const std::filesystem::path& path,
    const std::vector<native::Candidate>& candidates) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) { throw std::runtime_error("cannot create persistence benchmark corpus"); }
    for (const auto& candidate : candidates) { output << candidate.k << ' ' << candidate.n << '\n'; }
}

[[nodiscard]] std::string res64(const std::uint32_t k) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << k;
    return output.str();
}

[[nodiscard]] std::vector<native::Candidate> corpus(const std::size_t count) {
    std::vector<native::Candidate> candidates;
    candidates.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        candidates.push_back({static_cast<std::uint32_t>(1'000'001U + 2U * index), 996'551U});
    }
    return candidates;
}

[[nodiscard]] native::BatchExecution execute(const native::BatchRequest& request) {
    native::BatchExecution execution;
    execution.phases.parameter_build_us = 1U;
    execution.phases.host_to_device_us = 1U;
    execution.phases.witness_selection_us = 1U;
    execution.phases.a_pow_k_gpu_us = 1U;
    execution.phases.main_ntt_gpu_us = 1U;
    execution.phases.gerbicz_gpu_us = 1U;
    execution.phases.final_reduce_gpu_us = 1U;
    execution.phases.device_to_host_us = 1U;
    execution.phases.worker_total_us = 8U;
    for (std::size_t lane = 0U; lane < request.candidates.size(); ++lane) {
        execution.results.push_back({
            lane, request.candidates[lane], "COMPOSITE", 3U,
            res64(request.candidates[lane].k), "GERBICZ_PASS", {}});
    }
    return execution;
}

}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("primeforge-persistence-benchmark-" + std::to_string(
            Clock::now().time_since_epoch().count()));
    try {
        primeforge::PortableSha256Provider sha256;
        std::filesystem::create_directories(root);
        std::cout << "candidates\tbatches\twall_ms\tresults_bytes\tcheckpoint_bytes\tbytes_per_candidate\tcandidates_per_second\n";
        for (const std::size_t count : {1'024U, 2'048U, 4'096U}) {
            const auto directory = root / std::to_string(count);
            std::filesystem::create_directories(directory);
            const auto candidates = corpus(count);
            write_candidates(directory / "remaining.txt", candidates);
            write_candidates(directory / "parent-completed.txt", {});
            native::CampaignConfig config;
            config.campaign_directory = directory;
            config.remaining_queue_path = directory / "remaining.txt";
            config.parent_completed_path = directory / "parent-completed.txt";
            config.campaign_id = "sha256:persistence-scaling-" + std::to_string(count);
            config.parent_campaign_id = "sha256:persistence-parent";
            config.engine_commit = "persistence-benchmark";
            config.engine_binary_sha256 = std::string(64U, 'a');
            config.survivor_list_sha256 = std::string(64U, 'b');
            config.batch_size = native::max_batch_size;
            config.supervisor_pid = 1U;

            const auto start = Clock::now();
            const auto summary = native::run_campaign(
                config,
                [](const native::BatchRequest& request, const native::ResourceSink&,
                   const native::RuntimeIdsSink&, const native::StopRequested&) {
                    return execute(request);
                },
                sha256);
            const auto wall_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (summary.state != "COMPLETE_NO_PRIME" || summary.completed_this_resume != count) {
                throw std::runtime_error("persistence benchmark campaign did not complete exactly");
            }
            const auto result_bytes = std::filesystem::file_size(directory / "candidate-results.tsv");
            const auto checkpoint_bytes = std::filesystem::file_size(directory / "campaign.checkpoint.json");
            std::cout << count << '\t' << (count + native::max_batch_size - 1U) / native::max_batch_size
                      << '\t' << std::fixed << std::setprecision(3) << wall_ms
                      << '\t' << result_bytes << '\t' << checkpoint_bytes
                      << '\t' << std::setprecision(6) << static_cast<double>(result_bytes) / count
                      << '\t' << std::setprecision(3) << 1000.0 * count / wall_ms << '\n';
        }
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::cerr << "primeforge-native-persistence-benchmark: " << error.what() << '\n';
        return 1;
    }
}
