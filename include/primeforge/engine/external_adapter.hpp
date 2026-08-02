// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"
#include "primeforge/engine/engine_adapter.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace primeforge::engine {

enum class ExternalEngineKind {
    primesieve,
    flint,
    pari_gp,
    openpfgw,
    genefer22,
    mersenne_prpll,
    mlucas,
    gmp_ecm,
    fixture
};

struct ExternalAdapterConfig {
    ExternalEngineKind kind{ExternalEngineKind::fixture};
    std::string stable_id;
    std::string parser_version;
    std::filesystem::path executable;
    std::string expected_executable_sha256;
    std::vector<std::string> supported_families;
    std::chrono::milliseconds timeout{30'000};
    std::uint64_t memory_limit_bytes{512U * 1024U * 1024U};
    bool can_produce_proof{};
    bool can_resume{};
};

struct PreparedExternalRun {
    std::string job_id;
    std::string family_id;
    std::string decimal_input;
    std::filesystem::path working_directory;
    std::filesystem::path checkpoint_directory;
    std::vector<std::string> arguments;
};

struct ExternalProcessResult {
    int exit_code{};
    bool timed_out{};
    std::filesystem::path raw_stdout_path;
    std::filesystem::path raw_stderr_path;
};

struct ArtifactVerification {
    bool valid{};
    std::string executable_sha256;
    std::vector<std::string> errors;
};

struct CostEstimate {
    bool known{};
    std::uint64_t estimated_nanoseconds{};
    std::string basis{"UNKNOWN"};
};

[[nodiscard]] EngineResult parse_external_output(
    ExternalEngineKind kind,
    std::string_view parser_version,
    std::string_view raw_stdout,
    std::string_view raw_stderr);

class ExternalEngineAdapter final : public EngineAdapter {
public:
    ExternalEngineAdapter(ExternalAdapterConfig config, const Sha256Provider& sha256);

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] EngineCapabilities capabilities() const override;
    [[nodiscard]] bool supports(const EngineRequest& request) const noexcept override;
    [[nodiscard]] EngineResult run(const EngineRequest& request) override;

    [[nodiscard]] bool supports(
        std::string_view family, std::uint64_t bit_length, std::string_view proof_policy) const noexcept;
    [[nodiscard]] CostEstimate estimate_cost(std::size_t batch_size) const noexcept;
    [[nodiscard]] PreparedExternalRun prepare(const EngineRequest& request) const;
    [[nodiscard]] ExternalProcessResult run_process(const PreparedExternalRun& prepared) const;
    [[nodiscard]] EngineResult parse(const ExternalProcessResult& process) const;
    [[nodiscard]] ArtifactVerification verify_artifacts(const ExternalProcessResult& process) const;
    [[nodiscard]] std::string report_capabilities() const;

private:
    ExternalAdapterConfig config_;
    const Sha256Provider* sha256_{};
};

[[nodiscard]] std::string to_string(ExternalEngineKind kind);

}  // namespace primeforge::engine
