// SPDX-License-Identifier: Apache-2.0

#include "primeforge/cuda/prp_batch.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace primeforge::cuda_backend {
namespace {

constexpr std::size_t maximum_capacity = 1'048'576U;
constexpr unsigned int threads_per_block = 256U;

void require_cuda(const cudaError_t status, const char *const operation) {
    if (status != cudaSuccess) {
        throw std::runtime_error(std::string{operation} + ": " + cudaGetErrorString(status));
    }
}

[[nodiscard]] __device__ std::uint64_t add_mod(const std::uint64_t left, const std::uint64_t right,
                                               const std::uint64_t modulus) noexcept {
    return left >= modulus - right ? left - (modulus - right) : left + right;
}

[[nodiscard]] __device__ std::uint64_t multiply_mod_exact(std::uint64_t left, std::uint64_t right,
                                                          const std::uint64_t modulus) noexcept {
    left %= modulus;
    right %= modulus;
    std::uint64_t result = 0U;
    while (right != 0U) {
        if ((right & 1U) != 0U) result = add_mod(result, left, modulus);
        right >>= 1U;
        if (right != 0U) left = add_mod(left, left, modulus);
    }
    return result;
}

[[nodiscard]] __device__ std::uint64_t power_mod(std::uint64_t base, std::uint64_t exponent,
                                                 const std::uint64_t modulus) noexcept {
    std::uint64_t result = 1U;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = multiply_mod_exact(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0U) base = multiply_mod_exact(base, base, modulus);
    }
    return result;
}

[[nodiscard]] __device__ bool is_small_prime_or_divisible(const std::uint64_t value,
                                                          bool &decided) noexcept {
    constexpr std::uint64_t small_primes[] = {2U,  3U,  5U,  7U,  11U, 13U,
                                              17U, 19U, 23U, 29U, 31U, 37U};
    for (const auto prime : small_primes) {
        if (value == prime) {
            decided = true;
            return true;
        }
        if (value % prime == 0U) {
            decided = true;
            return false;
        }
    }
    decided = false;
    return false;
}

[[nodiscard]] __device__ bool is_base2_strong_prp(const std::uint64_t value) noexcept {
    if (value < 2U) return false;
    bool decided = false;
    const bool small_result = is_small_prime_or_divisible(value, decided);
    if (decided) return small_result;

    auto odd_part = value - 1U;
    unsigned int shifts = 0U;
    while ((odd_part & 1U) == 0U) {
        odd_part >>= 1U;
        ++shifts;
    }
    auto residue = power_mod(2U, odd_part, value);
    if (residue == 1U || residue == value - 1U) return true;
    for (unsigned int index = 1U; index < shifts; ++index) {
        residue = multiply_mod_exact(residue, residue, value);
        if (residue == value - 1U) return true;
        if (residue == 1U) return false;
    }
    return false;
}

__global__ void base2_strong_prp_kernel(const std::uint64_t *const values,
                                        prp::Base2StrongPrpVerdict *const verdicts,
                                        const std::size_t count) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= count) return;
    verdicts[index] = is_base2_strong_prp(values[index])
                          ? prp::Base2StrongPrpVerdict::probable_prime
                          : prp::Base2StrongPrpVerdict::composite;
}

class CudaBase2StrongPrpBatchBackend final : public prp::Base2StrongPrpBatchBackend {
public:
    CudaBase2StrongPrpBatchBackend(const std::size_t capacity, const int device_index)
        : capacity_{capacity}, device_index_{device_index} {
        static_assert(sizeof(prp::Base2StrongPrpVerdict) == sizeof(std::uint8_t));
        if (capacity_ == 0U || capacity_ > maximum_capacity) {
            throw std::invalid_argument("CUDA PRP batch capacity is out of range");
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
            require_cuda(cudaGetDeviceProperties(&properties, device_index_),
                         "cudaGetDeviceProperties");
            if (std::string_view{properties.name} != "NVIDIA GeForce RTX 5080" ||
                properties.major != 12 || properties.minor != 0) {
                throw std::runtime_error("CUDA PRP backend requires the target RTX "
                                         "5080 compute capability 12.0");
            }
            require_cuda(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking),
                         "cudaStreamCreateWithFlags");
            require_cuda(cudaMalloc(reinterpret_cast<void **>(&device_values_),
                                    capacity_ * sizeof(std::uint64_t)),
                         "cudaMalloc PRP values");
            require_cuda(cudaMalloc(reinterpret_cast<void **>(&device_verdicts_),
                                    capacity_ * sizeof(prp::Base2StrongPrpVerdict)),
                         "cudaMalloc PRP verdicts");
        } catch (...) {
            release();
            throw;
        }
    }

    CudaBase2StrongPrpBatchBackend(const CudaBase2StrongPrpBatchBackend &) = delete;
    CudaBase2StrongPrpBatchBackend &operator=(const CudaBase2StrongPrpBatchBackend &) = delete;
    ~CudaBase2StrongPrpBatchBackend() override { release(); }

    [[nodiscard]] std::string_view id() const noexcept override {
        return "primeforge.cuda.base2-strong-prp-u64.v1";
    }
    [[nodiscard]] std::size_t capacity() const noexcept override { return capacity_; }

    void test(const std::span<const std::uint64_t> values,
              const std::span<prp::Base2StrongPrpVerdict> verdicts) override {
        if (values.size() != verdicts.size()) {
            throw std::invalid_argument("CUDA PRP input/output sizes differ");
        }
        if (values.size() > capacity_) {
            throw std::length_error("CUDA PRP batch exceeds backend capacity");
        }
        if (values.empty()) return;

        require_cuda(cudaSetDevice(device_index_), "cudaSetDevice PRP execute");
        require_cuda(cudaMemcpyAsync(device_values_, values.data(), values.size_bytes(),
                                     cudaMemcpyHostToDevice, stream_),
                     "cudaMemcpyAsync PRP values host-to-device");
        const auto blocks =
            static_cast<unsigned int>((values.size() + threads_per_block - 1U) / threads_per_block);
        base2_strong_prp_kernel<<<blocks, threads_per_block, 0U, stream_>>>(
            device_values_, device_verdicts_, values.size());
        require_cuda(cudaGetLastError(), "base2_strong_prp_kernel launch");
        require_cuda(cudaMemcpyAsync(verdicts.data(), device_verdicts_, verdicts.size_bytes(),
                                     cudaMemcpyDeviceToHost, stream_),
                     "cudaMemcpyAsync PRP verdicts device-to-host");
        require_cuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize PRP");
    }

private:
    void release() noexcept {
        if (device_verdicts_ != nullptr) {
            static_cast<void>(cudaFree(device_verdicts_));
            device_verdicts_ = nullptr;
        }
        if (device_values_ != nullptr) {
            static_cast<void>(cudaFree(device_values_));
            device_values_ = nullptr;
        }
        if (stream_ != nullptr) {
            static_cast<void>(cudaStreamDestroy(stream_));
            stream_ = nullptr;
        }
    }

    std::size_t capacity_{};
    int device_index_{};
    cudaStream_t stream_{};
    std::uint64_t *device_values_{};
    prp::Base2StrongPrpVerdict *device_verdicts_{};
};

}  // namespace

std::unique_ptr<prp::Base2StrongPrpBatchBackend>
make_cuda_base2_strong_prp_batch_backend(const std::size_t capacity, const int device_index) {
    return std::make_unique<CudaBase2StrongPrpBatchBackend>(capacity, device_index);
}

}  // namespace primeforge::cuda_backend
