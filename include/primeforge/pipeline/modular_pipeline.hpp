// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"
#include "primeforge/cuda/modular_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace primeforge::pipeline {

struct ModularPipelineOptions {
    std::size_t batch_size{8'192U};
};

struct ModularPipelineResult {
    std::vector<std::uint64_t> residues;
    std::filesystem::path checkpoint_path;
    std::filesystem::path event_log_path;
    std::filesystem::path result_ledger_path;
    std::size_t resumed_tasks{};
    std::size_t committed_tasks{};
    std::size_t submitted_batches{};
    std::size_t maximum_in_flight_batches{};
    bool stopped{};
    bool complete{};
};

class ModularBatchPipeline {
public:
    ModularBatchPipeline(
        std::vector<std::unique_ptr<cuda_backend::ModularBatchBackend>> backends,
        ModularPipelineOptions options,
        const Sha256Provider& sha256_provider);

    [[nodiscard]] ModularPipelineResult run(
        std::span<const cuda_backend::ModularMultiplyTask> tasks,
        const std::filesystem::path& working_directory,
        const std::string& campaign_id,
        std::stop_token stop_token = {});

private:
    std::vector<std::unique_ptr<cuda_backend::ModularBatchBackend>> backends_;
    ModularPipelineOptions options_;
    const Sha256Provider& sha256_provider_;
    std::string backend_id_;
};

}  // namespace primeforge::pipeline
