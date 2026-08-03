// SPDX-License-Identifier: Apache-2.0

#include "primeforge/adaptive_bound/adaptive_bound.hpp"
#include "primeforge/core/sha256.hpp"
#include "primeforge/prp/base2_batch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(const bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function> void expect_failure(Function &&function, const std::string &message) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}

[[nodiscard]] std::vector<primeforge::prp::Base2StrongPrpVerdict> verify_exact(
    primeforge::prp::Base2StrongPrpBatchBackend &backend,
    const std::vector<std::uint64_t> &values) {
    std::vector<primeforge::prp::Base2StrongPrpVerdict> verdicts(values.size());
    const auto metrics = backend.test(values, verdicts);
    check(metrics.total_ns > 0U && metrics.cpu_ns == metrics.total_ns &&
              metrics.host_to_device_ns == 0U && metrics.kernel_ns == 0U &&
              metrics.device_to_host_ns == 0U && !metrics.used_accelerator,
          "CPU backend reports an exact CPU-only timing classification");
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const bool observed =
            verdicts[index] == primeforge::prp::Base2StrongPrpVerdict::probable_prime;
        const bool expected =
            primeforge::adaptive_bound::is_base2_strong_probable_prime_u64(values[index]);
        check(observed == expected, "batched CPU PRP differs from scalar oracle");
    }
    return verdicts;
}

class RecordingBackend final : public primeforge::prp::Base2StrongPrpBatchBackend {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "recording"; }
    [[nodiscard]] std::size_t capacity() const noexcept override { return 4U; }

    [[nodiscard]] primeforge::prp::Base2StrongPrpBatchMetrics test(
        const std::span<const std::uint64_t> values,
        const std::span<primeforge::prp::Base2StrongPrpVerdict> verdicts) override {
        check(values.size() == verdicts.size(), "recording backend receives matching spans");
        batch_sizes.push_back(values.size());
        for (std::size_t index = 0U; index < values.size(); ++index) {
            verdicts[index] = values[index] % 2U == 0U
                                  ? primeforge::prp::Base2StrongPrpVerdict::composite
                                  : primeforge::prp::Base2StrongPrpVerdict::probable_prime;
        }
        const auto size = static_cast<std::uint64_t>(values.size());
        return {100U + size, 10U + size, 20U + size, 30U + size, 40U + size,
                values.size() == 2U};
    }

    std::vector<std::size_t> batch_sizes;
};

class OverflowBackend final : public primeforge::prp::Base2StrongPrpBatchBackend {
public:
    explicit OverflowBackend(const std::size_t metric_index) : metric_index_{metric_index} {}

    [[nodiscard]] std::string_view id() const noexcept override { return "overflow"; }
    [[nodiscard]] std::size_t capacity() const noexcept override { return 1U; }

    [[nodiscard]] primeforge::prp::Base2StrongPrpBatchMetrics test(
        const std::span<const std::uint64_t> values,
        const std::span<primeforge::prp::Base2StrongPrpVerdict> verdicts) override {
        check(values.size() == 1U && verdicts.size() == 1U,
              "overflow backend receives one-value chunks");
        verdicts.front() = primeforge::prp::Base2StrongPrpVerdict::composite;
        const auto value = calls_++ == 0U ? std::numeric_limits<std::uint64_t>::max() : 1U;
        primeforge::prp::Base2StrongPrpBatchMetrics metrics;
        switch (metric_index_) {
            case 0U: metrics.total_ns = value; break;
            case 1U: metrics.cpu_ns = value; break;
            case 2U: metrics.host_to_device_ns = value; break;
            case 3U: metrics.kernel_ns = value; break;
            case 4U: metrics.device_to_host_ns = value; break;
            default: throw std::logic_error("invalid overflow metric index");
        }
        return metrics;
    }

private:
    std::size_t metric_index_{};
    std::size_t calls_{};
};

void test_bounded_batch_runner() {
    constexpr std::array<std::uint64_t, 10> values{
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U};
    std::array<primeforge::prp::Base2StrongPrpVerdict, values.size()> verdicts{};
    RecordingBackend backend;
    const auto metrics = primeforge::prp::test_base2_strong_prp_in_batches(
        backend, values, verdicts, 4U);
    check(backend.batch_sizes == std::vector<std::size_t>{4U, 4U, 2U},
          "bounded runner preserves two complete chunks and one partial tail");
    check(metrics.total_ns == 310U && metrics.cpu_ns == 40U &&
              metrics.host_to_device_ns == 70U && metrics.kernel_ns == 100U &&
              metrics.device_to_host_ns == 130U && metrics.used_accelerator,
          "bounded runner aggregates every metric and accelerator use");
    for (std::size_t index = 0U; index < verdicts.size(); ++index) {
        const auto expected = index % 2U == 0U
                                  ? primeforge::prp::Base2StrongPrpVerdict::composite
                                  : primeforge::prp::Base2StrongPrpVerdict::probable_prime;
        check(verdicts[index] == expected, "bounded runner preserves verdict order");
    }

    expect_failure(
        [&] {
            static_cast<void>(primeforge::prp::test_base2_strong_prp_in_batches(
                backend, values, verdicts, 0U));
        },
        "bounded runner rejects zero batch size");
    expect_failure(
        [&] {
            static_cast<void>(primeforge::prp::test_base2_strong_prp_in_batches(
                backend, values, verdicts, 5U));
        },
        "bounded runner rejects a batch larger than backend capacity");
    std::array<primeforge::prp::Base2StrongPrpVerdict, 1> wrong_size{};
    expect_failure(
        [&] {
            static_cast<void>(primeforge::prp::test_base2_strong_prp_in_batches(
                backend, values, wrong_size, 4U));
        },
        "bounded runner rejects mismatched aggregate spans");

    constexpr std::array<std::uint64_t, 2> overflow_values{2U, 4U};
    std::array<primeforge::prp::Base2StrongPrpVerdict, overflow_values.size()>
        overflow_verdicts{};
    for (std::size_t metric_index = 0U; metric_index < 5U; ++metric_index) {
        OverflowBackend overflow_backend{metric_index};
        expect_failure(
            [&] {
                static_cast<void>(primeforge::prp::test_base2_strong_prp_in_batches(
                    overflow_backend, overflow_values, overflow_verdicts, 1U));
            },
            "bounded runner rejects every metric accumulator overflow");
    }
}

}  // namespace

int main() {
    try {
        std::vector<std::uint64_t> values;
        values.reserve(20'000U);
        for (std::uint64_t value = 0U; value < 20'000U; ++value) {
            values.push_back(value);
        }
        for (const std::uint64_t value :
             {2'047ULL, 1'373'653ULL, 3'215'031'751ULL, 18'446'744'073'709'551'557ULL,
              18'446'744'073'709'551'615ULL}) {
            values.push_back(value);
        }

        auto cpu = primeforge::prp::make_cpu_base2_strong_prp_batch_backend(values.size(), 4U);
        check(cpu->capacity() == values.size() && !cpu->id().empty(),
              "CPU backend exposes stable identity and capacity");
        const auto monolithic_verdicts = verify_exact(*cpu, values);

        auto chunked = primeforge::prp::make_cpu_base2_strong_prp_batch_backend(32U, 4U);
        std::vector<primeforge::prp::Base2StrongPrpVerdict> chunked_verdicts(values.size());
        const auto chunked_metrics = primeforge::prp::test_base2_strong_prp_in_batches(
            *chunked, values, chunked_verdicts, 32U);
        check(chunked_metrics.total_ns > 0U &&
                  chunked_metrics.cpu_ns == chunked_metrics.total_ns &&
                  chunked_metrics.host_to_device_ns == 0U &&
                  chunked_metrics.kernel_ns == 0U &&
                  chunked_metrics.device_to_host_ns == 0U &&
                  !chunked_metrics.used_accelerator,
              "chunked CPU metrics aggregate as CPU-only work");
        check(chunked_verdicts == monolithic_verdicts,
              "32-value chunks exactly match monolithic verdicts including partial tail");
        const primeforge::PortableSha256Provider sha256;
        const auto monolithic_hash = primeforge::sha256_to_hex(
            sha256.digest(std::as_bytes(std::span{monolithic_verdicts})));
        const auto chunked_hash = primeforge::sha256_to_hex(
            sha256.digest(std::as_bytes(std::span{chunked_verdicts})));
        check(chunked_hash == monolithic_hash,
              "32-value chunks preserve the exact monolithic result hash");

        test_bounded_batch_runner();

        std::vector<primeforge::prp::Base2StrongPrpVerdict> wrong_size(1U);
        expect_failure([&] { static_cast<void>(cpu->test(values, wrong_size)); },
                       "CPU backend rejects mismatched output size");
        expect_failure(
            [] { static_cast<void>(primeforge::prp::make_cpu_base2_strong_prp_batch_backend(0U)); },
            "CPU backend rejects zero capacity");

        std::cout << "prp.cpu.vectors=" << values.size() << '\n'
                  << "prp.cpu.scalar_agreement=YES\n"
                  << "PrimeForge PRP batch tests: PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "PrimeForge PRP batch tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
