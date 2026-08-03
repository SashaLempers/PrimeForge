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

class Base2StrongPrpBatchBackend {
public:
    virtual ~Base2StrongPrpBatchBackend() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::size_t capacity() const noexcept = 0;

    virtual void test(std::span<const std::uint64_t> values,
                      std::span<Base2StrongPrpVerdict> verdicts) = 0;
};

[[nodiscard]] std::unique_ptr<Base2StrongPrpBatchBackend>
make_cpu_base2_strong_prp_batch_backend(std::size_t capacity, unsigned int worker_count = 0U);

}  // namespace primeforge::prp
