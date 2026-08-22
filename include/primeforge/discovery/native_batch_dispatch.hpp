// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace primeforge::discovery {

struct NativeBatchDispatchRequest {
    std::string gpu_name;
    std::uint64_t transform_length{};
    std::uint32_t engine_max_batch_size{32U};
    std::optional<std::uint32_t> explicit_batch_size;
};

struct NativeBatchDispatchDecision {
    std::uint32_t batch_size{1U};
    std::string policy_id;
    std::string reason;
    std::string plan_policy_id;
    std::string plan_mode;
    bool hardware_profile_matched{};
};

// Selects concurrency from the measured hardware/transform regime. An explicit
// valid size always wins. Unknown hardware or transform classes fall back to B1.
[[nodiscard]] NativeBatchDispatchDecision select_native_batch(
    const NativeBatchDispatchRequest& request);

}  // namespace primeforge::discovery
