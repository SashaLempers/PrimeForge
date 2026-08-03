// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/prp/base2_batch.hpp"

#include <cstddef>
#include <memory>

namespace primeforge::cuda_backend {

[[nodiscard]] std::unique_ptr<prp::Base2StrongPrpBatchBackend>
make_cuda_base2_strong_prp_batch_backend(std::size_t capacity, int device_index = 0);

// Uses the CPU below the measured crossover and initializes CUDA only when a
// sufficiently large batch first reaches the accelerator path.
[[nodiscard]] std::unique_ptr<prp::Base2StrongPrpBatchBackend>
make_auto_cuda_base2_strong_prp_batch_backend(
    std::size_t capacity,
    std::size_t accelerator_minimum_values,
    int device_index = 0);

}  // namespace primeforge::cuda_backend
