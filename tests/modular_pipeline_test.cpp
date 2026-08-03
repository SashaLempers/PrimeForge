// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/cuda/modular_batch.hpp"
#include "primeforge/math/mul128.hpp"
#include "primeforge/pipeline/modular_pipeline.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"

#include <atomic>
#include <chrono>
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
#include <thread>
#include <utility>
#include <vector>

namespace {

using Backend = primeforge::cuda_backend::ModularBatchBackend;
using Task = primeforge::cuda_backend::ModularMultiplyTask;

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Action>
void expect_throw(Action&& action, const std::string& message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

class FakeBackend final : public Backend {
public:
    FakeBackend(
        const std::size_t capacity,
        std::string id = "primeforge.fake.modular.v1",
        std::stop_source* stop_source = nullptr,
        const bool request_stop = false,
        const bool corrupt = false,
        const bool fail = false,
        const unsigned delay_milliseconds = 0U)
        : capacity_(capacity),
          id_(std::move(id)),
          stop_source_(stop_source),
          request_stop_(request_stop),
          corrupt_(corrupt),
          fail_(fail),
          delay_milliseconds_(delay_milliseconds) {}

    [[nodiscard]] std::string_view id() const noexcept override { return id_; }
    [[nodiscard]] std::size_t capacity() const noexcept override { return capacity_; }

    void multiply_mod(
        const std::span<const Task> tasks,
        const std::span<std::uint64_t> residues) override {
        ++calls_;
        if (request_stop_ && calls_ == 1U && stop_source_ != nullptr) {
            static_cast<void>(stop_source_->request_stop());
        }
        if (delay_milliseconds_ != 0U) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds{delay_milliseconds_});
        }
        if (fail_) throw std::runtime_error("injected backend failure");
        if (tasks.size() != residues.size()) throw std::invalid_argument("size mismatch");
        for (std::size_t index = 0U; index < tasks.size(); ++index) {
            residues[index] = primeforge::math::multiply_mod_portable_reference(
                tasks[index].left, tasks[index].right, tasks[index].modulus);
        }
        if (corrupt_ && !residues.empty()) residues.front() ^= 1U;
    }

private:
    std::size_t capacity_{};
    std::string id_;
    std::stop_source* stop_source_{};
    bool request_stop_{};
    bool corrupt_{};
    bool fail_{};
    unsigned delay_milliseconds_{};
    std::atomic_uint calls_{};
};

[[nodiscard]] std::vector<std::unique_ptr<Backend>> make_backends(
    const std::size_t count,
    const std::size_t capacity,
    std::stop_source* stop_source = nullptr,
    const bool request_stop = false) {
    std::vector<std::unique_ptr<Backend>> result;
    for (std::size_t index = 0U; index < count; ++index) {
        result.push_back(std::make_unique<FakeBackend>(
            capacity,
            "primeforge.fake.modular.v1",
            stop_source,
            request_stop && index + 1U == count,
            false,
            false,
            static_cast<unsigned>(count - index)));
    }
    return result;
}

[[nodiscard]] std::vector<Task> make_tasks(const std::size_t count) {
    std::vector<Task> result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto value = static_cast<std::uint64_t>(index);
        result.push_back({
            value,
            value * 0x9e3779b97f4a7c15ULL + 17U,
            index % 29U == 0U ? std::numeric_limits<std::uint64_t>::max()
                              : static_cast<std::uint64_t>(index * 2U + 3U)});
    }
    return result;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read test file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void verify_expected(
    const std::span<const Task> tasks,
    const std::span<const std::uint64_t> residues) {
    check(tasks.size() == residues.size(), "pipeline result size mismatch");
    for (std::size_t index = 0U; index < tasks.size(); ++index) {
        check(
            residues[index] == primeforge::math::multiply_mod_portable_reference(
                                   tasks[index].left,
                                   tasks[index].right,
                                   tasks[index].modulus),
            "pipeline residue mismatch at " + std::to_string(index));
    }
}

void test_buffer_counts_and_order(
    const std::filesystem::path& root,
    const std::span<const Task> tasks,
    const primeforge::Sha256Provider& sha256) {
    std::string reference_ledger;
    for (std::size_t buffers = 1U; buffers <= 3U; ++buffers) {
        const auto directory = root / ("buffers-" + std::to_string(buffers));
        primeforge::pipeline::ModularBatchPipeline pipeline(
            make_backends(buffers, 17U), {17U}, sha256);
        const auto result = pipeline.run(tasks, directory, "portable-complete");
        check(result.complete && !result.stopped, "complete pipeline status");
        check(result.committed_tasks == tasks.size(), "complete committed count");
        check(result.resumed_tasks == 0U, "fresh pipeline resumed unexpectedly");
        check(result.maximum_in_flight_batches == buffers, "buffer count not exercised");
        check(result.maximum_in_flight_batches <= buffers, "back-pressure bound exceeded");
        verify_expected(tasks, result.residues);
        const auto ledger = read_file(result.result_ledger_path);
        if (reference_ledger.empty()) reference_ledger = ledger;
        check(ledger == reference_ledger, "buffer count changed durable result bytes");
    }
}

void test_stop_resume_and_suffix_rollback(
    const std::filesystem::path& root,
    const std::span<const Task> tasks,
    const primeforge::Sha256Provider& sha256) {
    const auto directory = root / "resume";
    std::stop_source stop;
    primeforge::pipeline::ModularBatchPipeline interrupted(
        make_backends(3U, 17U, &stop, true), {17U}, sha256);
    const auto first = interrupted.run(
        tasks, directory, "portable-resume", stop.get_token());
    check(first.stopped && !first.complete, "cooperative stop status");
    check(first.committed_tasks == 51U, "in-flight batches were not drained exactly");
    check(first.maximum_in_flight_batches == 3U, "triple buffering was not exercised");

    {
        std::ofstream suffix(first.result_ledger_path, std::ios::binary | std::ios::app);
        suffix << "uncommitted-suffix";
    }

    primeforge::pipeline::ModularBatchPipeline resumed(
        make_backends(3U, 17U), {17U}, sha256);
    const auto second = resumed.run(tasks, directory, "portable-resume");
    check(second.complete && !second.stopped, "resumed pipeline did not complete");
    check(second.resumed_tasks == first.committed_tasks, "resume index mismatch");
    check(second.committed_tasks == tasks.size(), "resumed committed count");
    verify_expected(tasks, second.residues);

    const auto reference = read_file(root / "buffers-3" / "pipeline.results.jsonl");
    check(read_file(second.result_ledger_path) == reference,
          "interruption/resume changed final durable bytes");

    std::vector<Task> changed(tasks.begin(), tasks.end());
    changed.back().right ^= 1U;
    primeforge::pipeline::ModularBatchPipeline mismatched(
        make_backends(2U, 17U), {17U}, sha256);
    expect_throw(
        [&] { static_cast<void>(mismatched.run(changed, directory, "portable-resume")); },
        "changed task identity was accepted on resume");
}

void test_failure_and_corruption(
    const std::filesystem::path& root,
    const std::span<const Task> tasks,
    const primeforge::Sha256Provider& sha256) {
    const auto divergence_directory = root / "divergence";
    std::vector<std::unique_ptr<Backend>> corrupt;
    corrupt.push_back(std::make_unique<FakeBackend>(17U, "primeforge.fake.modular.v1",
                                                    nullptr, false, true));
    primeforge::pipeline::ModularBatchPipeline divergent(
        std::move(corrupt), {17U}, sha256);
    expect_throw(
        [&] {
            static_cast<void>(divergent.run(
                tasks.first(17U), divergence_directory, "portable-divergence"));
        },
        "CPU verification accepted a divergent backend");
    primeforge::runtime::CheckpointManager checkpoints{sha256};
    const auto state = checkpoints.load(divergence_directory / "pipeline.checkpoint.json");
    check(state.sequence == 0U, "divergent batch advanced checkpoint");

    const auto failure_directory = root / "backend-failure";
    std::vector<std::unique_ptr<Backend>> failing;
    failing.push_back(std::make_unique<FakeBackend>(17U, "primeforge.fake.modular.v1",
                                                    nullptr, false, false, true));
    primeforge::pipeline::ModularBatchPipeline failed(std::move(failing), {17U}, sha256);
    expect_throw(
        [&] {
            static_cast<void>(failed.run(
                tasks.first(17U), failure_directory, "portable-failure"));
        },
        "backend failure was accepted");

    const auto corrupt_directory = root / "corrupt-checkpoint";
    std::stop_source pre_stopped;
    static_cast<void>(pre_stopped.request_stop());
    primeforge::pipeline::ModularBatchPipeline stopped(
        make_backends(1U, 17U), {17U}, sha256);
    const auto empty = stopped.run(
        tasks, corrupt_directory, "portable-corrupt", pre_stopped.get_token());
    check(empty.stopped && empty.committed_tasks == 0U, "pre-stop checkpoint status");
    {
        std::fstream file(
            empty.checkpoint_path,
            std::ios::binary | std::ios::in | std::ios::out);
        check(static_cast<bool>(file), "checkpoint opened for corruption");
        file.seekp(20);
        file.put('X');
    }
    primeforge::pipeline::ModularBatchPipeline corrupt_resume(
        make_backends(1U, 17U), {17U}, sha256);
    expect_throw(
        [&] {
            static_cast<void>(corrupt_resume.run(
                tasks, corrupt_directory, "portable-corrupt"));
        },
        "corrupt checkpoint was accepted");

    const auto ledger_directory = root / "corrupt-ledger";
    primeforge::pipeline::ModularBatchPipeline ledger_source(
        make_backends(2U, 17U), {17U}, sha256);
    const auto completed = ledger_source.run(
        tasks.first(34U), ledger_directory, "portable-corrupt-ledger");
    check(completed.complete, "ledger corruption fixture did not complete");
    {
        std::fstream file(
            completed.result_ledger_path,
            std::ios::binary | std::ios::in | std::ios::out);
        check(static_cast<bool>(file), "ledger opened for corruption");
        file.seekg(30);
        const auto previous = file.get();
        file.seekp(30);
        file.put(previous == '7' ? '8' : '7');
    }
    primeforge::pipeline::ModularBatchPipeline ledger_resume(
        make_backends(2U, 17U), {17U}, sha256);
    expect_throw(
        [&] {
            static_cast<void>(ledger_resume.run(
                tasks.first(34U), ledger_directory, "portable-corrupt-ledger"));
        },
        "corrupt durable ledger was accepted");
}

void test_constructor_contract(const primeforge::Sha256Provider& sha256) {
    expect_throw(
        [&] {
            primeforge::pipeline::ModularBatchPipeline pipeline({}, {1U}, sha256);
        },
        "empty backend set was accepted");
    expect_throw(
        [&] {
            primeforge::pipeline::ModularBatchPipeline pipeline(
                make_backends(1U, 17U), {0U}, sha256);
        },
        "zero batch size was accepted");
    expect_throw(
        [&] {
            primeforge::pipeline::ModularBatchPipeline pipeline(
                make_backends(1U, 16U), {17U}, sha256);
        },
        "undersized backend capacity was accepted");
    std::vector<std::unique_ptr<Backend>> mixed;
    mixed.push_back(std::make_unique<FakeBackend>(17U, "backend-a"));
    mixed.push_back(std::make_unique<FakeBackend>(17U, "backend-b"));
    expect_throw(
        [&] {
            primeforge::pipeline::ModularBatchPipeline pipeline(
                std::move(mixed), {17U}, sha256);
        },
        "mixed backend ids were accepted");
}

}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      "primeforge-modular-pipeline-test";
    try {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        primeforge::PortableSha256Provider sha256;
        const auto tasks = make_tasks(257U);
        test_constructor_contract(sha256);
        test_buffer_counts_and_order(root, tasks, sha256);
        test_stop_resume_and_suffix_rollback(root, tasks, sha256);
        test_failure_and_corruption(root, tasks, sha256);
        std::filesystem::remove_all(root);
        std::cout << "PrimeForge modular pipeline tests: PASS "
                  << "(1/2/3 buffers, stop/resume, suffix rollback, faults)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge modular pipeline tests: FAIL: " << error.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
