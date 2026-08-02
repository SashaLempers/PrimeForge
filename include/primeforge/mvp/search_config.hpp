// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/congruence/compiler.hpp"
#include "primeforge/core/sha256.hpp"
#include "primeforge/work/work_unit.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace primeforge::mvp {

struct EngineExecutable {
    std::filesystem::path path;
    std::string expected_sha256;
};

struct SearchConfig {
    std::string schema{"primeforge.search.v1"};
    std::string campaign_name;
    std::string family{"proth"};
    std::string expression{"k*2^n+1"};
    std::uint64_t k_start{};
    std::uint64_t k_stop{};
    std::uint64_t k_step{};
    std::uint64_t n_start{};
    std::uint64_t n_stop{};
    std::uint64_t n_step{};
    bool require_odd_k{};
    bool require_k_less_than_power{};
    std::uint64_t work_unit_candidates{};
    std::uint64_t sieve_maximum_prime{};
    std::uint64_t checkpoint_every_candidates{};
    std::filesystem::path output_directory;
    EngineExecutable pari_gp;
    EngineExecutable flint;
};

struct CandidateCoordinates {
    std::uint64_t flat_index{};
    std::uint64_t k{};
    std::uint64_t n{};
    std::uint64_t value{};
};

struct CampaignPlan {
    std::string configuration_sha256;
    std::string campaign_id;
    std::uint64_t candidate_count{};
    std::vector<work::WorkUnit> work_units;
    work::CoverageVerification coverage;
};

[[nodiscard]] SearchConfig parse_search_config(std::string_view yaml);
[[nodiscard]] SearchConfig load_search_config(const std::filesystem::path& path);
[[nodiscard]] std::string canonical_search_config(const SearchConfig& config);
[[nodiscard]] std::string render_search_config_yaml(const SearchConfig& config);
[[nodiscard]] std::string search_config_sha256(
    const SearchConfig& config, const Sha256Provider& sha256);
[[nodiscard]] std::uint64_t candidate_count(const SearchConfig& config);
[[nodiscard]] CandidateCoordinates candidate_at(
    const SearchConfig& config, std::uint64_t flat_index);
[[nodiscard]] congruence::AffineExponentialFamily make_affine_family(
    const SearchConfig& config);
[[nodiscard]] CampaignPlan build_campaign_plan(
    const SearchConfig& config, const Sha256Provider& sha256);
[[nodiscard]] std::string canonical_inspection(const CampaignPlan& plan);

}  // namespace primeforge::mvp
