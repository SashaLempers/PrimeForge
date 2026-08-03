// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace primeforge::prp {

enum class Base2StrongPrpVerdict : std::uint8_t {
    composite = 0U,
    probable_prime = 1U,
};

struct Base2StrongPrpBatchMetrics {
    std::uint64_t total_ns{};
    std::uint64_t cpu_ns{};
    std::uint64_t host_to_device_ns{};
    std::uint64_t kernel_ns{};
    std::uint64_t device_to_host_ns{};
    bool used_accelerator{};
};

class Base2StrongPrpBatchBackend {
public:
    virtual ~Base2StrongPrpBatchBackend() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::size_t capacity() const noexcept = 0;

    [[nodiscard]] virtual Base2StrongPrpBatchMetrics test(
        std::span<const std::uint64_t> values,
        std::span<Base2StrongPrpVerdict> verdicts) = 0;
};

[[nodiscard]] Base2StrongPrpBatchMetrics test_base2_strong_prp_in_batches(
    Base2StrongPrpBatchBackend& backend,
    std::span<const std::uint64_t> values,
    std::span<Base2StrongPrpVerdict> verdicts,
    std::size_t batch_size);

[[nodiscard]] std::unique_ptr<Base2StrongPrpBatchBackend>
make_cpu_base2_strong_prp_batch_backend(std::size_t capacity, unsigned int worker_count = 0U);

}  // namespace primeforge::prp
