// SPDX-License-Identifier: Apache-2.0

#include "primeforge/cuda/modular_batch.hpp"
#include "primeforge/math/mul128.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Task = primeforge::cuda_backend::ModularMultiplyTask;

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Exception, typename Action>
void expect_exception(Action&& action, const std::string& message) {
    try {
        action();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::vector<Task> make_boundary_tasks() {
    constexpr std::array<std::uint64_t, 10U> values{
        0U,
        1U,
        2U,
        3U,
        std::numeric_limits<std::uint32_t>::max(),
        std::uint64_t{1} << 32U,
        (std::uint64_t{1} << 63U) - 1U,
        std::uint64_t{1} << 63U,
        std::numeric_limits<std::uint64_t>::max() - 1U,
        std::numeric_limits<std::uint64_t>::max(),
    };
    constexpr std::array<std::uint64_t, 10U> moduli{
        1U,
        2U,
        3U,
        5U,
        std::numeric_limits<std::uint32_t>::max(),
        std::uint64_t{1} << 32U,
        (std::uint64_t{1} << 63U) - 1U,
        std::uint64_t{1} << 63U,
        std::numeric_limits<std::uint64_t>::max() - 1U,
        std::numeric_limits<std::uint64_t>::max(),
    };

    std::vector<Task> tasks;
    tasks.reserve(values.size() * values.size() * moduli.size());
    for (const auto modulus : moduli) {
        for (const auto left : values) {
            for (const auto right : values) {
                tasks.push_back({left, right, modulus});
            }
        }
    }
    return tasks;
}

void append_random_tasks(std::vector<Task>& tasks, const std::size_t count) {
    std::uint64_t state = 0x4f1bbcdc0a5e3d27ULL;
    for (std::size_t index = 0U; index < count; ++index) {
        auto modulus = splitmix64(state);
        if (modulus == 0U) modulus = std::numeric_limits<std::uint64_t>::max();
        if (index % 17U == 0U) modulus = 1U;
        if (index % 31U == 0U) modulus = std::numeric_limits<std::uint64_t>::max();
        if (index % 47U == 0U) modulus = std::uint64_t{1} << 63U;
        tasks.push_back({splitmix64(state), splitmix64(state), modulus});
    }
}

void verify_batches(
    primeforge::cuda_backend::ModularBatchBackend& backend,
    const std::span<const Task> tasks) {
    std::vector<std::uint64_t> expected(tasks.size());
    for (std::size_t index = 0U; index < tasks.size(); ++index) {
        const auto& task = tasks[index];
        expected[index] = primeforge::math::multiply_mod_portable_reference(
            task.left, task.right, task.modulus);
        check(
            primeforge::math::multiply_mod(task.left, task.right, task.modulus) ==
                expected[index],
            "optimized CPU/reference divergence at vector " + std::to_string(index));
    }

    std::vector<std::uint64_t> observed(tasks.size());
    for (std::size_t offset = 0U; offset < tasks.size(); offset += backend.capacity()) {
        const auto count = std::min(backend.capacity(), tasks.size() - offset);
        backend.multiply_mod(tasks.subspan(offset, count),
                             std::span{observed}.subspan(offset, count));
    }
    check(observed == expected, "CUDA modular batch diverged from CPU reference");
    for (std::size_t index = 0U; index < tasks.size(); ++index) {
        check(observed[index] < tasks[index].modulus, "CUDA residue is not reduced");
    }

    const auto repeat_count = std::min(backend.capacity(), tasks.size());
    std::vector<std::uint64_t> repeated(repeat_count);
    backend.multiply_mod(tasks.first(repeat_count), repeated);
    check(
        std::equal(repeated.begin(), repeated.end(), observed.begin()),
        "CUDA modular batch is not deterministic");
}

void test_contract_errors(primeforge::cuda_backend::ModularBatchBackend& backend) {
    backend.multiply_mod(std::span<const Task>{}, std::span<std::uint64_t>{});

    const std::array<Task, 1U> valid{{{1U, 2U, 3U}}};
    std::array<std::uint64_t, 1U> output{};
    expect_exception<std::invalid_argument>(
        [&] { backend.multiply_mod(valid, std::span<std::uint64_t>{}); },
        "size mismatch was accepted");

    const std::array<Task, 1U> zero_modulus{{{1U, 2U, 0U}}};
    expect_exception<std::invalid_argument>(
        [&] { backend.multiply_mod(zero_modulus, output); },
        "zero modulus was accepted");

    std::vector<Task> oversized(backend.capacity() + 1U, {1U, 2U, 3U});
    std::vector<std::uint64_t> oversized_output(oversized.size());
    expect_exception<std::length_error>(
        [&] { backend.multiply_mod(oversized, oversized_output); },
        "oversized batch was accepted");
}

}  // namespace

int main() {
    try {
        constexpr std::size_t capacity = 8'192U;
        constexpr std::size_t random_count = 100'000U;
        auto backend = primeforge::cuda_backend::make_cuda_modular_batch_backend(capacity);
        check(backend != nullptr, "CUDA modular backend factory returned null");
        check(backend->id() == "primeforge.cuda.modular-u64.v1", "unstable backend id");
        check(backend->capacity() == capacity, "backend capacity mismatch");

        expect_exception<std::invalid_argument>(
            [] { static_cast<void>(primeforge::cuda_backend::make_cuda_modular_batch_backend(0U)); },
            "zero backend capacity was accepted");
        expect_exception<std::invalid_argument>(
            [] {
                static_cast<void>(primeforge::cuda_backend::make_cuda_modular_batch_backend(
                    1'048'577U));
            },
            "excessive backend capacity was accepted");
        expect_exception<std::invalid_argument>(
            [] {
                static_cast<void>(primeforge::cuda_backend::make_cuda_modular_batch_backend(
                    1U, -1));
            },
            "negative CUDA device index was accepted");
        expect_exception<std::invalid_argument>(
            [] {
                static_cast<void>(primeforge::cuda_backend::make_cuda_modular_batch_backend(
                    1U, 1'000));
            },
            "unavailable CUDA device index was accepted");

        test_contract_errors(*backend);
        auto tasks = make_boundary_tasks();
        const auto boundary_count = tasks.size();
        append_random_tasks(tasks, random_count);
        verify_batches(*backend, tasks);

        std::cout << "cuda.modular.backend_id=" << backend->id() << '\n'
                  << "cuda.modular.capacity=" << backend->capacity() << '\n'
                  << "cuda.modular.boundary_vectors=" << boundary_count << '\n'
                  << "cuda.modular.random_vectors=" << random_count << '\n'
                  << "cuda.modular.total_vectors=" << tasks.size() << '\n'
                  << "cuda.modular.status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cuda.modular.status=FAIL: " << error.what() << '\n';
        return 1;
    }
}
