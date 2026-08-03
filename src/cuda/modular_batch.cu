// SPDX-License-Identifier: Apache-2.0

#include "primeforge/cuda/modular_batch.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace primeforge::cuda_backend {
namespace {

constexpr std::size_t maximum_capacity = 1'048'576U;
constexpr unsigned int threads_per_block = 256U;

void require_cuda(const cudaError_t status, const char* const operation) {
    if (status != cudaSuccess) {
        throw std::runtime_error(
            std::string{operation} + ": " + cudaGetErrorString(status));
    }
}

[[nodiscard]] __device__ std::uint64_t add_mod(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::uint64_t modulus) noexcept {
    return left >= modulus - right ? left - (modulus - right) : left + right;
}

[[nodiscard]] __device__ std::uint64_t multiply_mod_exact(
    std::uint64_t left,
    std::uint64_t right,
    const std::uint64_t modulus) noexcept {
    left %= modulus;
    right %= modulus;
    std::uint64_t result = 0U;
    while (right != 0U) {
        if ((right & 1U) != 0U) {
            result = add_mod(result, left, modulus);
        }
        right >>= 1U;
        if (right != 0U) {
            left = add_mod(left, left, modulus);
        }
    }
    return result;
}

__global__ void modular_multiply_kernel(
    const ModularMultiplyTask* const tasks,
    std::uint64_t* const residues,
    const std::size_t count) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const auto task = tasks[index];
    residues[index] = multiply_mod_exact(task.left, task.right, task.modulus);
}

class CudaModularBatchBackend final : public ModularBatchBackend {
public:
    CudaModularBatchBackend(const std::size_t capacity, const int device_index)
        : capacity_(capacity), device_index_(device_index) {
        if (capacity_ == 0U || capacity_ > maximum_capacity) {
            throw std::invalid_argument("CUDA modular batch capacity is out of range");
        }
        if (device_index_ < 0) {
            throw std::invalid_argument("CUDA device index must be nonnegative");
        }

        try {
            int device_count = 0;
            require_cuda(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount");
            if (device_index_ >= device_count) {
                throw std::invalid_argument("CUDA device index is unavailable");
            }
            require_cuda(cudaSetDevice(device_index_), "cudaSetDevice");

            cudaDeviceProp properties{};
            require_cuda(
                cudaGetDeviceProperties(&properties, device_index_),
                "cudaGetDeviceProperties");
            if (std::string_view{properties.name} != "NVIDIA GeForce RTX 5080" ||
                properties.major != 12 || properties.minor != 0) {
                throw std::runtime_error(
                    "CUDA modular backend requires the target RTX 5080 compute capability 12.0");
            }

            require_cuda(
                cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking),
                "cudaStreamCreateWithFlags");
            require_cuda(
                cudaMalloc(
                    reinterpret_cast<void**>(&device_tasks_),
                    capacity_ * sizeof(ModularMultiplyTask)),
                "cudaMalloc tasks");
            require_cuda(
                cudaMalloc(
                    reinterpret_cast<void**>(&device_residues_),
                    capacity_ * sizeof(std::uint64_t)),
                "cudaMalloc residues");
        } catch (...) {
            release();
            throw;
        }
    }

    CudaModularBatchBackend(const CudaModularBatchBackend&) = delete;
    CudaModularBatchBackend& operator=(const CudaModularBatchBackend&) = delete;

    ~CudaModularBatchBackend() override { release(); }

    [[nodiscard]] std::string_view id() const noexcept override {
        return "primeforge.cuda.modular-u64.v1";
    }

    [[nodiscard]] std::size_t capacity() const noexcept override { return capacity_; }

    void multiply_mod(
        const std::span<const ModularMultiplyTask> tasks,
        const std::span<std::uint64_t> residues) override {
        if (tasks.size() != residues.size()) {
            throw std::invalid_argument("CUDA modular input/output sizes differ");
        }
        if (tasks.size() > capacity_) {
            throw std::length_error("CUDA modular batch exceeds backend capacity");
        }
        for (const auto& task : tasks) {
            if (task.modulus == 0U) {
                throw std::invalid_argument("modulus must be nonzero");
            }
        }
        if (tasks.empty()) return;

        require_cuda(cudaSetDevice(device_index_), "cudaSetDevice execute");
        require_cuda(
            cudaMemcpyAsync(
                device_tasks_, tasks.data(), tasks.size_bytes(),
                cudaMemcpyHostToDevice, stream_),
            "cudaMemcpyAsync tasks host-to-device");

        const auto block_count = static_cast<unsigned int>(
            (tasks.size() + threads_per_block - 1U) / threads_per_block);
        modular_multiply_kernel<<<block_count, threads_per_block, 0U, stream_>>>(
            device_tasks_, device_residues_, tasks.size());
        require_cuda(cudaGetLastError(), "modular_multiply_kernel launch");

        require_cuda(
            cudaMemcpyAsync(
                residues.data(), device_residues_, residues.size_bytes(),
                cudaMemcpyDeviceToHost, stream_),
            "cudaMemcpyAsync residues device-to-host");
        require_cuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize");
    }

private:
    void release() noexcept {
        if (device_residues_ != nullptr) {
            static_cast<void>(cudaFree(device_residues_));
            device_residues_ = nullptr;
        }
        if (device_tasks_ != nullptr) {
            static_cast<void>(cudaFree(device_tasks_));
            device_tasks_ = nullptr;
        }
        if (stream_ != nullptr) {
            static_cast<void>(cudaStreamDestroy(stream_));
            stream_ = nullptr;
        }
    }

    std::size_t capacity_{};
    int device_index_{};
    cudaStream_t stream_{};
    ModularMultiplyTask* device_tasks_{};
    std::uint64_t* device_residues_{};
};

}  // namespace

std::unique_ptr<ModularBatchBackend> make_cuda_modular_batch_backend(
    const std::size_t capacity,
    const int device_index) {
    return std::make_unique<CudaModularBatchBackend>(capacity, device_index);
}

}  // namespace primeforge::cuda_backend
