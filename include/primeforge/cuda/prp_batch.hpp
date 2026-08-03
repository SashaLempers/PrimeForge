// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/prp/base2_batch.hpp"

#include <cstddef>
#include <memory>

namespace primeforge::cuda_backend {

[[nodiscard]] std::unique_ptr<prp::Base2StrongPrpBatchBackend>
make_cuda_base2_strong_prp_batch_backend(std::size_t capacity, int device_index = 0);

}  // namespace primeforge::cuda_backend
