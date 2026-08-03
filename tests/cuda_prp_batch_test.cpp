// SPDX-License-Identifier: Apache-2.0

#include "primeforge/adaptive_bound/adaptive_bound.hpp"
#include "primeforge/cuda/prp_batch.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(const bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    try {
        constexpr std::size_t vector_count = 65'536U;
        std::vector<std::uint64_t> values;
        values.reserve(vector_count);
        std::uint64_t state = 0x9e3779b97f4a7c15ULL;
        for (std::size_t index = 0U; index < vector_count; ++index) {
            state = state * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
            values.push_back(state | 1U);
        }
        values[0] = 0U;
        values[1] = 2U;
        values[2] = 2'047U;
        values[3] = 3'215'031'751ULL;
        values[4] = 18'446'744'073'709'551'557ULL;
        values[5] = 18'446'744'073'709'551'615ULL;

        auto cuda =
            primeforge::cuda_backend::make_cuda_base2_strong_prp_batch_backend(vector_count);
        std::vector<primeforge::prp::Base2StrongPrpVerdict> observed(values.size());
        auto cpu = primeforge::prp::make_cpu_base2_strong_prp_batch_backend(vector_count);
        std::vector<primeforge::prp::Base2StrongPrpVerdict> cpu_observed(values.size());
        auto automatic =
            primeforge::cuda_backend::make_auto_cuda_base2_strong_prp_batch_backend(
                vector_count, 512U);
        std::vector<primeforge::prp::Base2StrongPrpVerdict> automatic_observed(values.size());
        const auto automatic_cpu_metrics = automatic->test(
            std::span<const std::uint64_t>{values}.first(256U),
            std::span<primeforge::prp::Base2StrongPrpVerdict>{automatic_observed}.first(256U));
        const auto cuda_start = std::chrono::steady_clock::now();
        const auto cuda_metrics = cuda->test(values, observed);
        const auto cuda_end = std::chrono::steady_clock::now();
        const auto cpu_batch_start = std::chrono::steady_clock::now();
        const auto cpu_metrics = cpu->test(values, cpu_observed);
        const auto cpu_batch_end = std::chrono::steady_clock::now();
        const auto automatic_cuda_metrics = automatic->test(values, automatic_observed);
        check(!automatic_cpu_metrics.used_accelerator && automatic_cpu_metrics.cpu_ns > 0U,
              "automatic backend reports its CPU route");
        check(cuda_metrics.used_accelerator && cuda_metrics.total_ns > 0U &&
                  cuda_metrics.kernel_ns > 0U && cuda_metrics.cpu_ns == 0U,
              "CUDA backend reports measured accelerator phases");
        check(!cpu_metrics.used_accelerator && cpu_metrics.cpu_ns == cpu_metrics.total_ns,
              "CPU backend reports measured CPU time");
        check(automatic_cuda_metrics.used_accelerator,
              "automatic backend reports its CUDA route");
        std::size_t probable_count = 0U;
        const auto cpu_start = std::chrono::steady_clock::now();
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const bool expected =
                primeforge::adaptive_bound::is_base2_strong_probable_prime_u64(values[index]);
            const bool actual =
                observed[index] == primeforge::prp::Base2StrongPrpVerdict::probable_prime;
            check(actual == expected,
                  "CUDA candidate-level PRP differs from CPU scalar oracle at " +
                      std::to_string(index));
            check(cpu_observed[index] == observed[index],
                  "parallel CPU and CUDA batch PRP verdicts differ at " +
                      std::to_string(index));
            check(automatic_observed[index] == observed[index],
                  "automatic CPU/CUDA routing differs at " + std::to_string(index));
            if (actual) ++probable_count;
        }
        const auto cpu_end = std::chrono::steady_clock::now();
        const auto cuda_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                           cuda_end - cuda_start)
                                           .count();
        const auto cpu_batch_microseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(
                cpu_batch_end - cpu_batch_start)
                .count();
        const auto cpu_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                          cpu_end - cpu_start)
                                          .count();

        constexpr std::array<std::size_t, 13U> sweep_sizes{
            16U, 32U, 64U, 128U, 256U, 512U, 1'024U, 2'048U,
            4'096U, 8'192U, 16'384U, 32'768U, 65'536U};
        for (const auto count : sweep_sizes) {
            const auto input = std::span<const std::uint64_t>{values}.first(count);
            const auto cuda_output =
                std::span<primeforge::prp::Base2StrongPrpVerdict>{observed}.first(count);
            const auto cpu_output =
                std::span<primeforge::prp::Base2StrongPrpVerdict>{cpu_observed}.first(count);
            const auto gpu_begin = std::chrono::steady_clock::now();
            static_cast<void>(cuda->test(input, cuda_output));
            const auto gpu_end = std::chrono::steady_clock::now();
            const auto host_begin = std::chrono::steady_clock::now();
            static_cast<void>(cpu->test(input, cpu_output));
            const auto host_end = std::chrono::steady_clock::now();
            const auto gpu_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                    gpu_end - gpu_begin)
                                    .count();
            const auto host_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                     host_end - host_begin)
                                     .count();
            std::cout << "prp.sweep.count=" << count << ";cuda_us=" << gpu_us
                      << ";cpu_us=" << host_us << '\n';
        }

        std::cout << "cuda.prp.vectors=" << values.size() << '\n'
                  << "cuda.prp.probable=" << probable_count << '\n'
                  << "cuda.prp.batch_microseconds=" << cuda_microseconds << '\n'
                  << "cpu.prp.batch_microseconds=" << cpu_batch_microseconds << '\n'
                  << "cpu.prp.scalar_oracle_microseconds=" << cpu_microseconds << '\n'
                  << "cuda.prp.scalar_agreement=YES\n"
                  << "cuda.prp.auto_routing=YES\n"
                  << "cuda.prp.status=PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "CUDA PRP batch test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
