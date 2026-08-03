// SPDX-License-Identifier: Apache-2.0

#include "primeforge/prp/base2_batch.hpp"

#include "primeforge/adaptive_bound/adaptive_bound.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace primeforge::prp {
namespace {

[[nodiscard]] std::uint64_t checked_sum(
    const std::uint64_t left, const std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error("aggregated PRP batch timing overflow");
    }
    return left + right;
}

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

    [[nodiscard]] Base2StrongPrpBatchMetrics test(
        const std::span<const std::uint64_t> values,
        const std::span<Base2StrongPrpVerdict> verdicts) override {
        if (values.size() != verdicts.size()) {
            throw std::invalid_argument("CPU PRP input/output sizes differ");
        }
        if (values.size() > capacity_) {
            throw std::length_error("CPU PRP batch exceeds backend capacity");
        }
        if (values.empty()) return {};

        const auto started = std::chrono::steady_clock::now();

        const auto classify_range = [&](const std::size_t begin, const std::size_t end) {
            for (std::size_t index = begin; index < end; ++index) {
                verdicts[index] = adaptive_bound::is_base2_strong_probable_prime_u64(values[index])
                                      ? Base2StrongPrpVerdict::probable_prime
                                      : Base2StrongPrpVerdict::composite;
            }
        };

        constexpr std::size_t minimum_values_per_worker = 256U;
        const auto useful_workers =
            std::max<std::size_t>(1U, values.size() / minimum_values_per_worker);
        const auto workers = std::min<std::size_t>(worker_count_, useful_workers);
        if (workers == 1U) {
            classify_range(0U, values.size());
        } else {
            std::vector<std::jthread> threads;
            threads.reserve(workers);
            for (std::size_t worker = 0U; worker < workers; ++worker) {
                const auto begin = values.size() * worker / workers;
                const auto end = values.size() * (worker + 1U) / workers;
                threads.emplace_back(classify_range, begin, end);
            }
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started);
        const auto nanoseconds = static_cast<std::uint64_t>(elapsed.count());
        return {nanoseconds, nanoseconds, 0U, 0U, 0U, false};
    }

private:
    std::size_t capacity_{};
    unsigned int worker_count_{};
};

}  // namespace

Base2StrongPrpBatchMetrics test_base2_strong_prp_in_batches(
    Base2StrongPrpBatchBackend& backend,
    const std::span<const std::uint64_t> values,
    const std::span<Base2StrongPrpVerdict> verdicts,
    const std::size_t batch_size) {
    if (values.size() != verdicts.size()) {
        throw std::invalid_argument("batched PRP input/output sizes differ");
    }
    if (batch_size == 0U) {
        throw std::invalid_argument("PRP batch size must be nonzero");
    }
    if (batch_size > backend.capacity()) {
        throw std::length_error("PRP batch size exceeds backend capacity");
    }

    Base2StrongPrpBatchMetrics aggregate;
    for (std::size_t offset = 0U; offset < values.size();) {
        const auto count = std::min(batch_size, values.size() - offset);
        const auto metrics = backend.test(
            values.subspan(offset, count), verdicts.subspan(offset, count));
        aggregate.total_ns = checked_sum(aggregate.total_ns, metrics.total_ns);
        aggregate.cpu_ns = checked_sum(aggregate.cpu_ns, metrics.cpu_ns);
        aggregate.host_to_device_ns =
            checked_sum(aggregate.host_to_device_ns, metrics.host_to_device_ns);
        aggregate.kernel_ns = checked_sum(aggregate.kernel_ns, metrics.kernel_ns);
        aggregate.device_to_host_ns =
            checked_sum(aggregate.device_to_host_ns, metrics.device_to_host_ns);
        aggregate.used_accelerator = aggregate.used_accelerator || metrics.used_accelerator;
        offset += count;
    }
    return aggregate;
}

std::unique_ptr<Base2StrongPrpBatchBackend>
make_cpu_base2_strong_prp_batch_backend(const std::size_t capacity,
                                        const unsigned int worker_count) {
    return std::make_unique<CpuBase2StrongPrpBatchBackend>(capacity, worker_count);
}

}  // namespace primeforge::prp
