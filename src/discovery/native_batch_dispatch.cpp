// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/native_batch_dispatch.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace primeforge::discovery {
namespace {

[[nodiscard]] std::string lowercase(const std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

}  // namespace

NativeBatchDispatchDecision select_native_batch(const NativeBatchDispatchRequest& request) {
    if (request.engine_max_batch_size == 0U || request.engine_max_batch_size > 32U) {
        throw std::invalid_argument("native engine maximum batch size must be in 1..32");
    }
    if (request.explicit_batch_size) {
        if (*request.explicit_batch_size == 0U ||
            *request.explicit_batch_size > request.engine_max_batch_size) {
            throw std::invalid_argument("explicit native batch size exceeds engine capacity");
        }
        return {
            *request.explicit_batch_size,
            "primeforge.native-batch.explicit.v1",
            "EXPLICIT_REQUEST",
            "primeforge.native-plan.proth20-autotune.v1",
            "ENGINE_AUTOTUNE",
            true};
    }

    const auto normalized_gpu = lowercase(request.gpu_name);
    const bool rtx_5080 = normalized_gpu.find("rtx 5080") != std::string::npos;
    if (!rtx_5080 || request.transform_length == 0U) {
        return {
            1U,
            "primeforge.native-batch.safe-fallback.v1",
            rtx_5080 ? "UNKNOWN_TRANSFORM" : "UNMEASURED_HARDWARE",
            "primeforge.native-plan.safe-autotune.v1",
            "ENGINE_AUTOTUNE",
            false};
    }

    std::uint32_t measured_batch = 1U;
    std::string reason;
    if (request.transform_length <= 65'536U) {
        measured_batch = 32U;
        reason = "RTX5080_TRANSFORM_LE_65536";
    } else if (request.transform_length <= 131'072U) {
        measured_batch = 16U;
        reason = "RTX5080_TRANSFORM_LE_131072";
    } else if (request.transform_length <= 262'144U) {
        measured_batch = 8U;
        reason = "RTX5080_TRANSFORM_LE_262144";
    } else {
        reason = "UNMEASURED_TRANSFORM";
    }
    return {
        std::min(measured_batch, request.engine_max_batch_size),
        "primeforge.native-batch.rtx5080-scaling-20260822.v1",
        std::move(reason),
        "primeforge.native-plan.proth20-autotune.v1",
        "ENGINE_AUTOTUNE",
        measured_batch != 1U};
}

}  // namespace primeforge::discovery
