// SPDX-License-Identifier: Apache-2.0

#include <cuda_runtime_api.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require_cuda(const cudaError_t status, const char* const operation) {
    if (status != cudaSuccess) {
        throw std::runtime_error(
            std::string{operation} + ": " + cudaGetErrorString(status));
    }
}

class DeviceWords final {
public:
    explicit DeviceWords(const std::size_t count) : count_(count) {
        require_cuda(
            cudaMalloc(reinterpret_cast<void**>(&data_), count_ * sizeof(std::uint64_t)),
            "cudaMalloc");
    }

    DeviceWords(const DeviceWords&) = delete;
    DeviceWords& operator=(const DeviceWords&) = delete;

    ~DeviceWords() {
        if (data_ != nullptr) {
            static_cast<void>(cudaFree(data_));
        }
    }

    [[nodiscard]] std::uint64_t* get() noexcept { return data_; }
    [[nodiscard]] std::size_t bytes() const noexcept {
        return count_ * sizeof(std::uint64_t);
    }

private:
    std::uint64_t* data_{};
    std::size_t count_{};
};

__global__ void exact_transform(
    const std::uint64_t* const input,
    std::uint64_t* const output,
    const std::size_t count) {
    const auto index = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const auto value = input[index];
    output[index] = (value ^ 0x9e3779b97f4a7c15ULL) +
                    static_cast<std::uint64_t>(index) * 0x100000001b3ULL;
}

[[nodiscard]] std::vector<std::uint64_t> make_input() {
    constexpr std::size_t count = 4'096U;
    std::vector<std::uint64_t> result(count);
    constexpr std::array<std::uint64_t, 8U> boundaries{
        0U,
        1U,
        2U,
        3U,
        std::numeric_limits<std::uint32_t>::max(),
        std::uint64_t{1} << 32U,
        std::numeric_limits<std::uint64_t>::max() - 1U,
        std::numeric_limits<std::uint64_t>::max(),
    };
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index] = index < boundaries.size()
            ? boundaries[index]
            : static_cast<std::uint64_t>(index) * 0xd6e8feb86659fd93ULL;
    }
    return result;
}

[[nodiscard]] std::uint64_t expected_word(
    const std::uint64_t value,
    const std::size_t index) noexcept {
    return (value ^ 0x9e3779b97f4a7c15ULL) +
           static_cast<std::uint64_t>(index) * 0x100000001b3ULL;
}

}  // namespace

int main() {
    try {
        int device_count = 0;
        require_cuda(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount");
        if (device_count < 1) throw std::runtime_error("no CUDA device available");
        require_cuda(cudaSetDevice(0), "cudaSetDevice");

        cudaDeviceProp properties{};
        require_cuda(cudaGetDeviceProperties(&properties, 0), "cudaGetDeviceProperties");
        int driver_version = 0;
        int runtime_version = 0;
        require_cuda(cudaDriverGetVersion(&driver_version), "cudaDriverGetVersion");
        require_cuda(cudaRuntimeGetVersion(&runtime_version), "cudaRuntimeGetVersion");
        if (std::string{properties.name} != "NVIDIA GeForce RTX 5080" ||
            properties.major != 12 || properties.minor != 0) {
            throw std::runtime_error(
                "CUDA validation requires the target RTX 5080 compute capability 12.0");
        }
        if (runtime_version != 13'030 || driver_version < runtime_version) {
            throw std::runtime_error(
                "CUDA 13.3 runtime/driver API compatibility gate failed");
        }
        std::size_t free_bytes = 0U;
        std::size_t total_bytes = 0U;
        require_cuda(cudaMemGetInfo(&free_bytes, &total_bytes), "cudaMemGetInfo");

        const auto input = make_input();
        std::vector<std::uint64_t> observed(input.size());
        DeviceWords device_input(input.size());
        DeviceWords device_output(input.size());
        require_cuda(
            cudaMemcpy(
                device_input.get(), input.data(), device_input.bytes(),
                cudaMemcpyHostToDevice),
            "cudaMemcpy host-to-device");

        constexpr unsigned int block_size = 256U;
        const auto block_count = static_cast<unsigned int>(
            (input.size() + block_size - 1U) / block_size);
        exact_transform<<<block_count, block_size>>>(
            device_input.get(), device_output.get(), input.size());
        require_cuda(cudaGetLastError(), "exact_transform launch");
        require_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
        require_cuda(
            cudaMemcpy(
                observed.data(), device_output.get(), device_output.bytes(),
                cudaMemcpyDeviceToHost),
            "cudaMemcpy device-to-host");

        std::uint64_t checksum = 0xcbf29ce484222325ULL;
        for (std::size_t index = 0U; index < input.size(); ++index) {
            const auto expected = expected_word(input[index], index);
            if (observed[index] != expected) {
                throw std::runtime_error(
                    "CPU/GPU divergence at vector index " + std::to_string(index));
            }
            checksum ^= observed[index];
            checksum *= 0x100000001b3ULL;
        }

        std::cout << "cuda.device.name=" << properties.name << '\n'
                  << "cuda.device.compute_capability=" << properties.major << '.'
                  << properties.minor << '\n'
                  << "cuda.device.multiprocessors=" << properties.multiProcessorCount << '\n'
                  << "cuda.device.warp_size=" << properties.warpSize << '\n'
                  << "cuda.device.global_memory_bytes=" << properties.totalGlobalMem << '\n'
                  << "cuda.memory.free_bytes=" << free_bytes << '\n'
                  << "cuda.memory.total_bytes=" << total_bytes << '\n'
                  << "cuda.driver_api_version=" << driver_version << '\n'
                  << "cuda.runtime_version=" << runtime_version << '\n'
                  << "cuda.validation.vectors=" << input.size() << '\n'
                  << "cuda.validation.checksum=" << checksum << '\n'
                  << "cuda.validation.status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cuda.validation.status=FAIL: " << error.what() << '\n';
        return 1;
    }
}
