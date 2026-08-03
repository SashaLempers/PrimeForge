// SPDX-License-Identifier: Apache-2.0

#include "primeforge/prp/base2_batch.hpp"

#include "primeforge/adaptive_bound/adaptive_bound.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <vector>

namespace primeforge::prp {
namespace {

class CpuBase2StrongPrpBatchBackend final : public Base2StrongPrpBatchBackend {
public:
    CpuBase2StrongPrpBatchBackend(const std::size_t capacity, const unsigned int worker_count)
        : capacity_{capacity},
          worker_count_{worker_count == 0U ? std::max(1U, std::thread::hardware_concurrency())
                                           : worker_count} {
        if (capacity_ == 0U) {
            throw std::invalid_argument("CPU PRP batch capacity must be nonzero");
        }
    }

    [[nodiscard]] std::string_view id() const noexcept override {
        return "primeforge.cpu.base2-strong-prp-u64.v1";
    }

    [[nodiscard]] std::size_t capacity() const noexcept override { return capacity_; }

    void test(const std::span<const std::uint64_t> values,
              const std::span<Base2StrongPrpVerdict> verdicts) override {
        if (values.size() != verdicts.size()) {
            throw std::invalid_argument("CPU PRP input/output sizes differ");
        }
        if (values.size() > capacity_) {
            throw std::length_error("CPU PRP batch exceeds backend capacity");
        }
        if (values.empty()) return;

        const auto classify_range = [&](const std::size_t begin, const std::size_t end) {
            for (std::size_t index = begin; index < end; ++index) {
                verdicts[index] = adaptive_bound::is_base2_strong_probable_prime_u64(values[index])
                                      ? Base2StrongPrpVerdict::probable_prime
                                      : Base2StrongPrpVerdict::composite;
            }
        };

        // Avoid thread-launch overhead on the small exact regression domains.
        if (values.size() < 512U || worker_count_ == 1U) {
            classify_range(0U, values.size());
            return;
        }

        const auto workers = std::min<std::size_t>(worker_count_, values.size());
        std::vector<std::jthread> threads;
        threads.reserve(workers);
        for (std::size_t worker = 0U; worker < workers; ++worker) {
            const auto begin = values.size() * worker / workers;
            const auto end = values.size() * (worker + 1U) / workers;
            threads.emplace_back(classify_range, begin, end);
        }
    }

private:
    std::size_t capacity_{};
    unsigned int worker_count_{};
};

}  // namespace

std::unique_ptr<Base2StrongPrpBatchBackend>
make_cpu_base2_strong_prp_batch_backend(const std::size_t capacity,
                                        const unsigned int worker_count) {
    return std::make_unique<CpuBase2StrongPrpBatchBackend>(capacity, worker_count);
}

}  // namespace primeforge::prp
