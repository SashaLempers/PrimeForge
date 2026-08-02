// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace primeforge::work {

struct ParameterInterval {
    std::uint64_t begin{};
    std::uint64_t end{};

    [[nodiscard]] friend constexpr bool operator==(
        const ParameterInterval&, const ParameterInterval&) = default;
};

struct SieveBounds {
    std::uint64_t minimum_prime{};
    std::uint64_t maximum_prime{};
};

struct WorkUnit {
    std::string work_unit_id;
    std::string family_id;
    std::string canonical_definition_sha256;
    ParameterInterval interval;
    std::vector<std::string> constraints;
    std::string residue_compiler_version;
    SieveBounds sieve_bounds;
    std::string proof_policy;
    std::optional<std::uint64_t> seed;
};

struct CoverageVerification {
    bool valid{};
    std::vector<std::string> errors;
    std::string canonical_report_json;
};

enum class CheckpointFailurePoint {
    none,
    after_write_before_flush,
    after_flush_before_replace
};

[[nodiscard]] std::string canonical_work_unit_without_id(const WorkUnit& unit);
[[nodiscard]] std::string canonical_work_unit(const WorkUnit& unit);
[[nodiscard]] WorkUnit finalize_work_unit(WorkUnit unit, const Sha256Provider& sha256_provider);
[[nodiscard]] std::vector<WorkUnit> partition_work_units(
    const WorkUnit& prototype,
    std::uint64_t domain_begin,
    std::uint64_t domain_end,
    std::uint64_t maximum_unit_span,
    const Sha256Provider& sha256_provider);
[[nodiscard]] CoverageVerification verify_coverage(
    std::uint64_t expected_begin,
    std::uint64_t expected_end,
    const std::vector<WorkUnit>& units,
    const Sha256Provider& sha256_provider);

void write_checkpoint_atomically(
    const std::filesystem::path& target,
    const std::string& canonical_content,
    CheckpointFailurePoint failure_point = CheckpointFailurePoint::none);
[[nodiscard]] std::string read_checkpoint(const std::filesystem::path& target);

}  // namespace primeforge::work
