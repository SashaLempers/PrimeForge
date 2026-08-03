// SPDX-License-Identifier: Apache-2.0

#include "primeforge/cuda/prp_batch.hpp"

#include <cuda_runtime_api.h>

#include <cstddef>
#include <chrono>
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

[[nodiscard]] std::uint64_t elapsed_ns(cudaEvent_t begin, cudaEvent_t end) {
    float milliseconds = 0.0F;
    require_cuda(cudaEventElapsedTime(&milliseconds, begin, end), "cudaEventElapsedTime PRP");
    return static_cast<std::uint64_t>(milliseconds * 1'000'000.0F);
}

[[nodiscard]] __device__ std::uint64_t add_mod(const std::uint64_t left, const std::uint64_t right,
                                               const std::uint64_t modulus) noexcept {
    return left >= modulus - right ? left - (modulus - right) : left + right;
}

[[nodiscard]] __device__ std::uint64_t montgomery_inverse(
    const std::uint64_t odd_modulus) noexcept {
    std::uint64_t inverse = odd_modulus;
#pragma unroll
    for (unsigned int iteration = 0U; iteration < 6U; ++iteration) {
        inverse *= 2U - odd_modulus * inverse;
    }
    return 0U - inverse;
}

[[nodiscard]] __device__ std::uint64_t montgomery_multiply(
    const std::uint64_t left, const std::uint64_t right, const std::uint64_t modulus,
    const std::uint64_t negative_inverse) noexcept {
    const auto product_low = left * right;
    const auto product_high = __umul64hi(left, right);
    const auto factor = product_low * negative_inverse;
    const auto correction_low = factor * modulus;
    const auto correction_high = __umul64hi(factor, modulus);
    const auto low_sum = product_low + correction_low;
    const auto low_carry = static_cast<std::uint64_t>(low_sum < product_low);
    const auto high_sum = product_high + correction_high;
    const bool high_overflow = high_sum < product_high;
    const auto reduced = high_sum + low_carry;
    const bool carry_overflow = reduced < high_sum;
    if (high_overflow || carry_overflow) return reduced - modulus;
    return reduced >= modulus ? reduced - modulus : reduced;
}

[[nodiscard]] __device__ std::uint64_t power_mod_montgomery(
    std::uint64_t exponent, const std::uint64_t modulus,
    const std::uint64_t negative_inverse, const std::uint64_t one_montgomery) noexcept {
    std::uint64_t result = one_montgomery;
    std::uint64_t base = add_mod(one_montgomery, one_montgomery, modulus);
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = montgomery_multiply(result, base, modulus, negative_inverse);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = montgomery_multiply(base, base, modulus, negative_inverse);
        }
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
    const auto negative_inverse = montgomery_inverse(value);
    constexpr std::uint64_t maximum_u64 = ~std::uint64_t{0};
    const auto one_montgomery = ((maximum_u64 % value) + 1U) % value;
    const auto minus_one_montgomery = value - one_montgomery;
    auto residue = power_mod_montgomery(
        odd_part, value, negative_inverse, one_montgomery);
    if (residue == one_montgomery || residue == minus_one_montgomery) return true;
    for (unsigned int index = 1U; index < shifts; ++index) {
        residue = montgomery_multiply(residue, residue, value, negative_inverse);
        if (residue == minus_one_montgomery) return true;
        if (residue == one_montgomery) return false;
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
            require_cuda(cudaEventCreate(&start_event_), "cudaEventCreate PRP start");
            require_cuda(cudaEventCreate(&h2d_event_), "cudaEventCreate PRP H2D");
            require_cuda(cudaEventCreate(&kernel_event_), "cudaEventCreate PRP kernel");
            require_cuda(cudaEventCreate(&d2h_event_), "cudaEventCreate PRP D2H");
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

    [[nodiscard]] prp::Base2StrongPrpBatchMetrics test(
        const std::span<const std::uint64_t> values,
        const std::span<prp::Base2StrongPrpVerdict> verdicts) override {
        if (values.size() != verdicts.size()) {
            throw std::invalid_argument("CUDA PRP input/output sizes differ");
        }
        if (values.size() > capacity_) {
            throw std::length_error("CUDA PRP batch exceeds backend capacity");
        }
        if (values.empty()) return {};

        const auto started = std::chrono::steady_clock::now();
        require_cuda(cudaSetDevice(device_index_), "cudaSetDevice PRP execute");
        require_cuda(cudaEventRecord(start_event_, stream_), "cudaEventRecord PRP start");
        require_cuda(cudaMemcpyAsync(device_values_, values.data(), values.size_bytes(),
                                     cudaMemcpyHostToDevice, stream_),
                     "cudaMemcpyAsync PRP values host-to-device");
        require_cuda(cudaEventRecord(h2d_event_, stream_), "cudaEventRecord PRP H2D");
        const auto blocks =
            static_cast<unsigned int>((values.size() + threads_per_block - 1U) / threads_per_block);
        base2_strong_prp_kernel<<<blocks, threads_per_block, 0U, stream_>>>(
            device_values_, device_verdicts_, values.size());
        require_cuda(cudaGetLastError(), "base2_strong_prp_kernel launch");
        require_cuda(cudaEventRecord(kernel_event_, stream_), "cudaEventRecord PRP kernel");
        require_cuda(cudaMemcpyAsync(verdicts.data(), device_verdicts_, verdicts.size_bytes(),
                                     cudaMemcpyDeviceToHost, stream_),
                     "cudaMemcpyAsync PRP verdicts device-to-host");
        require_cuda(cudaEventRecord(d2h_event_, stream_), "cudaEventRecord PRP D2H");
        require_cuda(cudaEventSynchronize(d2h_event_), "cudaEventSynchronize PRP D2H");
        const auto total = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started);
        return {static_cast<std::uint64_t>(total.count()),
                0U,
                elapsed_ns(start_event_, h2d_event_),
                elapsed_ns(h2d_event_, kernel_event_),
                elapsed_ns(kernel_event_, d2h_event_),
                true};
    }

private:
    void release() noexcept {
        if (d2h_event_ != nullptr) {
            static_cast<void>(cudaEventDestroy(d2h_event_));
            d2h_event_ = nullptr;
        }
        if (kernel_event_ != nullptr) {
            static_cast<void>(cudaEventDestroy(kernel_event_));
            kernel_event_ = nullptr;
        }
        if (h2d_event_ != nullptr) {
            static_cast<void>(cudaEventDestroy(h2d_event_));
            h2d_event_ = nullptr;
        }
        if (start_event_ != nullptr) {
            static_cast<void>(cudaEventDestroy(start_event_));
            start_event_ = nullptr;
        }
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
    cudaEvent_t start_event_{};
    cudaEvent_t h2d_event_{};
    cudaEvent_t kernel_event_{};
    cudaEvent_t d2h_event_{};
    std::uint64_t *device_values_{};
    prp::Base2StrongPrpVerdict *device_verdicts_{};
};

class AutoCudaBase2StrongPrpBatchBackend final : public prp::Base2StrongPrpBatchBackend {
public:
    AutoCudaBase2StrongPrpBatchBackend(
        const std::size_t capacity, const std::size_t accelerator_minimum_values,
        const int device_index)
        : fallback_{prp::make_cpu_base2_strong_prp_batch_backend(capacity)},
          capacity_{capacity}, accelerator_minimum_values_{accelerator_minimum_values},
          device_index_{device_index},
          id_{"primeforge.auto.cpu-cuda.base2-strong-prp-u64.v1[min=" +
              std::to_string(accelerator_minimum_values) + "]"} {
        if (capacity_ == 0U || capacity_ > maximum_capacity ||
            accelerator_minimum_values_ == 0U ||
            accelerator_minimum_values_ > capacity_ || device_index_ < 0) {
            throw std::invalid_argument("automatic CUDA PRP routing boundary is invalid");
        }
    }

    [[nodiscard]] std::string_view id() const noexcept override { return id_; }
    [[nodiscard]] std::size_t capacity() const noexcept override { return capacity_; }

    [[nodiscard]] prp::Base2StrongPrpBatchMetrics test(
        const std::span<const std::uint64_t> values,
        const std::span<prp::Base2StrongPrpVerdict> verdicts) override {
        if (values.size() != verdicts.size()) {
            throw std::invalid_argument("automatic CUDA PRP input/output sizes differ");
        }
        if (values.size() > capacity_) {
            throw std::length_error("automatic CUDA PRP batch exceeds backend capacity");
        }
        if (values.size() < accelerator_minimum_values_) {
            return fallback_->test(values, verdicts);
        }
        if (accelerator_ == nullptr) {
            accelerator_ =
                std::make_unique<CudaBase2StrongPrpBatchBackend>(capacity_, device_index_);
        }
        return accelerator_->test(values, verdicts);
    }

private:
    std::unique_ptr<prp::Base2StrongPrpBatchBackend> fallback_;
    std::unique_ptr<prp::Base2StrongPrpBatchBackend> accelerator_;
    std::size_t capacity_{};
    std::size_t accelerator_minimum_values_{};
    int device_index_{};
    std::string id_;
};

}  // namespace

std::unique_ptr<prp::Base2StrongPrpBatchBackend>
make_cuda_base2_strong_prp_batch_backend(const std::size_t capacity, const int device_index) {
    return std::make_unique<CudaBase2StrongPrpBatchBackend>(capacity, device_index);
}

std::unique_ptr<prp::Base2StrongPrpBatchBackend>
make_auto_cuda_base2_strong_prp_batch_backend(
    const std::size_t capacity, const std::size_t accelerator_minimum_values,
    const int device_index) {
    return std::make_unique<AutoCudaBase2StrongPrpBatchBackend>(
        capacity, accelerator_minimum_values, device_index);
}

}  // namespace primeforge::cuda_backend
