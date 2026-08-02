// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"
#include "primeforge/engine/engine_adapter.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace primeforge::mvp {

struct VerificationSummary {
    std::string campaign_id;
    std::uint64_t record_count{};
    std::uint64_t composite_count{};
    std::uint64_t proven_prime_count{};
    std::uint64_t manifest_file_count{};
    bool valid{};
};

[[nodiscard]] VerificationSummary verify_campaign(
    const std::filesystem::path& results_path,
    const Sha256Provider& sha256,
    EngineAdapter& certificate_verifier,
    EngineAdapter& independent_engine);

}  // namespace primeforge::mvp
