// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace primeforge::runtime {

struct CheckpointState {
    std::string campaign_id;
    std::string opaque_payload;
    std::string progress_decimal;
    std::uint64_t sequence{};

    [[nodiscard]] friend bool operator==(const CheckpointState&, const CheckpointState&) = default;
};

class CheckpointManager {
public:
    explicit CheckpointManager(const Sha256Provider& sha256_provider);

    void save(const std::filesystem::path& path, const CheckpointState& state) const;
    [[nodiscard]] CheckpointState load(const std::filesystem::path& path) const;
    [[nodiscard]] bool valid(const std::filesystem::path& path) const noexcept;

private:
    const Sha256Provider& sha256_provider_;
};

} // namespace primeforge::runtime
