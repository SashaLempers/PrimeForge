// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/status.hpp"

#include <filesystem>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace primeforge {

struct EngineCapabilities {
    std::vector<std::string> supported_families;
    bool can_report_composite_witness{};
    bool can_produce_proof{};
    bool can_resume{};
};

struct EngineRequest {
    std::string job_id;
    std::string family_id;
    std::string canonical_input;
    std::filesystem::path working_directory;
};

struct EngineResult {
    CandidateStatus status;
    std::string diagnostics;
    std::string engine_executable_sha256;
    std::filesystem::path raw_stdout_path;
    std::filesystem::path raw_stderr_path;
    std::vector<std::filesystem::path> proof_artifact_paths;
};

class EngineAdapter {
public:
    virtual ~EngineAdapter() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual EngineCapabilities capabilities() const = 0;
    [[nodiscard]] virtual bool supports(const EngineRequest& request) const noexcept = 0;
    [[nodiscard]] virtual EngineResult run(const EngineRequest& request) = 0;
    [[nodiscard]] virtual std::size_t recommended_parallelism() const noexcept { return 1U; }
};

} // namespace primeforge
