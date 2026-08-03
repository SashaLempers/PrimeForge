// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace primeforge::cuda_backend {

struct ModularMultiplyTask {
    std::uint64_t left{};
    std::uint64_t right{};
    std::uint64_t modulus{};

    [[nodiscard]] friend constexpr bool operator==(
        const ModularMultiplyTask&, const ModularMultiplyTask&) = default;
};

class ModularBatchBackend {
public:
    virtual ~ModularBatchBackend() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::size_t capacity() const noexcept = 0;

    virtual void multiply_mod(
        std::span<const ModularMultiplyTask> tasks,
        std::span<std::uint64_t> residues) = 0;
};

[[nodiscard]] std::unique_ptr<ModularBatchBackend> make_cuda_modular_batch_backend(
    std::size_t capacity,
    int device_index = 0);

}  // namespace primeforge::cuda_backend
