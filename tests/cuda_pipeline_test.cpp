// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/cuda/modular_batch.hpp"
#include "primeforge/math/mul128.hpp"
#include "primeforge/pipeline/modular_pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Backend = primeforge::cuda_backend::ModularBatchBackend;
using Task = primeforge::cuda_backend::ModularMultiplyTask;

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::vector<Task> make_tasks(const std::size_t count) {
    std::vector<Task> result;
    result.reserve(count);
    std::uint64_t state = 0x928f3a61d725bc04ULL;
    for (std::size_t index = 0U; index < count; ++index) {
        auto modulus = splitmix64(state);
        if (modulus == 0U) modulus = 1U;
        if (index % 37U == 0U) modulus = 1U;
        if (index % 53U == 0U) modulus = std::numeric_limits<std::uint64_t>::max();
        result.push_back({splitmix64(state), splitmix64(state), modulus});
    }
    return result;
}

[[nodiscard]] std::vector<std::unique_ptr<Backend>> make_cuda_backends(
    const std::size_t count,
    const std::size_t capacity) {
    std::vector<std::unique_ptr<Backend>> result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        result.push_back(
            primeforge::cuda_backend::make_cuda_modular_batch_backend(capacity));
    }
    return result;
}

class StopRequestBackend final : public Backend {
public:
    StopRequestBackend(
        std::unique_ptr<Backend> delegate,
        std::stop_source& stop_source,
        const bool request_stop)
        : delegate_(std::move(delegate)),
          stop_source_(stop_source),
          request_stop_(request_stop) {}

    [[nodiscard]] std::string_view id() const noexcept override {
        return delegate_->id();
    }
    [[nodiscard]] std::size_t capacity() const noexcept override {
        return delegate_->capacity();
    }
    void multiply_mod(
        const std::span<const Task> tasks,
        const std::span<std::uint64_t> residues) override {
        if (request_stop_ && !requested_) {
            requested_ = true;
            static_cast<void>(stop_source_.request_stop());
        }
        delegate_->multiply_mod(tasks, residues);
    }

private:
    std::unique_ptr<Backend> delegate_;
    std::stop_source& stop_source_;
    bool request_stop_{};
    bool requested_{};
};

[[nodiscard]] std::vector<std::unique_ptr<Backend>> make_stopping_backends(
    const std::size_t count,
    const std::size_t capacity,
    std::stop_source& stop_source) {
    auto delegates = make_cuda_backends(count, capacity);
    std::vector<std::unique_ptr<Backend>> result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        result.push_back(std::make_unique<StopRequestBackend>(
            std::move(delegates[index]), stop_source, index + 1U == count));
    }
    return result;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read CUDA pipeline ledger");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void verify_results(
    const std::span<const Task> tasks,
    const std::span<const std::uint64_t> residues) {
    check(tasks.size() == residues.size(), "CUDA pipeline output size");
    for (std::size_t index = 0U; index < tasks.size(); ++index) {
        check(
            residues[index] == primeforge::math::multiply_mod_portable_reference(
                                   tasks[index].left,
                                   tasks[index].right,
                                   tasks[index].modulus),
            "CUDA pipeline divergence at task " + std::to_string(index));
    }
}

}  // namespace

int main() {
    constexpr std::size_t task_count = 4'097U;
    constexpr std::size_t batch_size = 257U;
    const auto root = std::filesystem::temp_directory_path() /
                      "primeforge-cuda-pipeline-test";
    try {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        primeforge::PortableSha256Provider sha256;
        const auto tasks = make_tasks(task_count);

        std::string reference_ledger;
        for (std::size_t buffers = 1U; buffers <= 3U; ++buffers) {
            primeforge::pipeline::ModularBatchPipeline pipeline(
                make_cuda_backends(buffers, batch_size), {batch_size}, sha256);
            const auto result = pipeline.run(
                tasks, root / ("complete-" + std::to_string(buffers)),
                "cuda-pipeline-complete");
            check(result.complete && result.committed_tasks == tasks.size(),
                  "CUDA pipeline complete status");
            check(result.maximum_in_flight_batches == buffers,
                  "CUDA pipeline buffer count not exercised");
            verify_results(tasks, result.residues);
            const auto ledger = read_file(result.result_ledger_path);
            if (reference_ledger.empty()) reference_ledger = ledger;
            check(ledger == reference_ledger,
                  "CUDA stream count changed durable result bytes");
        }

        std::stop_source stop_source;
        const auto resume_directory = root / "resume";
        primeforge::pipeline::ModularBatchPipeline interrupted(
            make_stopping_backends(3U, batch_size, stop_source),
            {batch_size}, sha256);
        const auto first = interrupted.run(
            tasks, resume_directory, "cuda-pipeline-resume",
            stop_source.get_token());
        check(first.stopped && first.committed_tasks == 3U * batch_size,
              "CUDA pipeline did not drain exactly three in-flight batches");
        {
            std::ofstream suffix(
                first.result_ledger_path, std::ios::binary | std::ios::app);
            suffix << "uncommitted-cuda-suffix";
        }

        primeforge::pipeline::ModularBatchPipeline resumed(
            make_cuda_backends(3U, batch_size), {batch_size}, sha256);
        const auto second = resumed.run(
            tasks, resume_directory, "cuda-pipeline-resume");
        check(second.complete && second.resumed_tasks == first.committed_tasks,
              "CUDA pipeline resume status");
        verify_results(tasks, second.residues);
        check(read_file(second.result_ledger_path) == reference_ledger,
              "CUDA interruption changed final durable ledger");

        std::filesystem::remove_all(root);
        std::cout << "cuda.pipeline.tasks=" << task_count << '\n'
                  << "cuda.pipeline.batch_size=" << batch_size << '\n'
                  << "cuda.pipeline.buffers_tested=1,2,3\n"
                  << "cuda.pipeline.interrupted_after=" << first.committed_tasks << '\n'
                  << "cuda.pipeline.resumed_from=" << second.resumed_tasks << '\n'
                  << "cuda.pipeline.status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cuda.pipeline.status=FAIL: " << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
