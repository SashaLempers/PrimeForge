// SPDX-License-Identifier: Apache-2.0

#include <cuda_runtime_api.h>

#include <cstddef>
#include <iostream>
#include <string>

namespace {

std::string version_string(const int version) {
    return std::to_string(version / 1000) + "." + std::to_string((version % 1000) / 10);
}

const char* yes_no(const int value) {
    return value != 0 ? "YES" : "NO";
}

bool check(const cudaError_t result, const char* operation) {
    if (result == cudaSuccess) {
        return true;
    }
    std::cerr << "cuda.error.operation=" << operation << '\n';
    std::cerr << "cuda.error.code=" << static_cast<int>(result) << '\n';
    std::cerr << "cuda.error.name=" << cudaGetErrorName(result) << '\n';
    std::cerr << "cuda.error.description=" << cudaGetErrorString(result) << '\n';
    return false;
}

} // namespace

int main() {
    int runtime_version = 0;
    int driver_version = 0;
    int device_count = 0;
    if (!check(cudaRuntimeGetVersion(&runtime_version), "cudaRuntimeGetVersion") ||
        !check(cudaDriverGetVersion(&driver_version), "cudaDriverGetVersion")) {
        return 1;
    }
    const cudaError_t count_result = cudaGetDeviceCount(&device_count);
    if (count_result == cudaErrorNoDevice) {
        device_count = 0;
    } else if (!check(count_result, "cudaGetDeviceCount")) {
        return 1;
    }

    std::cout << "cuda.runtime.version_raw=" << runtime_version << '\n';
    std::cout << "cuda.runtime.version=" << version_string(runtime_version) << '\n';
    std::cout << "cuda.driver_api.version_raw=" << driver_version << '\n';
    std::cout << "cuda.driver_api.version=" << version_string(driver_version) << '\n';
    std::cout << "cuda.compiled_runtime.version_raw=" << CUDART_VERSION << '\n';
    std::cout << "cuda.compiled_runtime.version=" << version_string(CUDART_VERSION) << '\n';
    std::cout << "cuda.device_count=" << device_count << '\n';

    for (int index = 0; index < device_count; ++index) {
        cudaDeviceProp properties{};
        if (!check(cudaGetDeviceProperties(&properties, index), "cudaGetDeviceProperties")) {
            return 1;
        }
        int memory_clock_khz = 0;
        if (!check(
                cudaDeviceGetAttribute(&memory_clock_khz, cudaDevAttrMemoryClockRate, index),
                "cudaDeviceGetAttribute(cudaDevAttrMemoryClockRate)")) {
            return 1;
        }
        const std::string prefix = "cuda.device." + std::to_string(index) + ".";
        std::cout << prefix << "name=" << properties.name << '\n';
        std::cout << prefix << "compute_capability=" << properties.major << '.' << properties.minor << '\n';
        std::cout << prefix << "total_global_memory_bytes=" << properties.totalGlobalMem << '\n';
        std::cout << prefix << "multiprocessor_count=" << properties.multiProcessorCount << '\n';
        std::cout << prefix << "warp_size=" << properties.warpSize << '\n';
        std::cout << prefix << "max_threads_per_block=" << properties.maxThreadsPerBlock << '\n';
        std::cout << prefix << "max_threads_dim=" << properties.maxThreadsDim[0] << ','
                  << properties.maxThreadsDim[1] << ',' << properties.maxThreadsDim[2] << '\n';
        std::cout << prefix << "max_grid_size=" << properties.maxGridSize[0] << ','
                  << properties.maxGridSize[1] << ',' << properties.maxGridSize[2] << '\n';
        std::cout << prefix << "l2_cache_bytes=" << properties.l2CacheSize << '\n';
        std::cout << prefix << "memory_bus_width_bits=" << properties.memoryBusWidth << '\n';
        std::cout << prefix << "memory_clock_khz=" << memory_clock_khz << '\n';
        std::cout << prefix << "async_engine_count=" << properties.asyncEngineCount << '\n';
        std::cout << prefix << "concurrent_kernels=" << yes_no(properties.concurrentKernels) << '\n';
        std::cout << prefix << "unified_addressing=" << yes_no(properties.unifiedAddressing) << '\n';
        std::cout << prefix << "managed_memory=" << yes_no(properties.managedMemory) << '\n';
        std::cout << prefix << "concurrent_managed_access=" << yes_no(properties.concurrentManagedAccess) << '\n';
        std::cout << prefix << "pageable_memory_access=" << yes_no(properties.pageableMemoryAccess) << '\n';
        std::cout << prefix << "can_map_host_memory=" << yes_no(properties.canMapHostMemory) << '\n';
        std::cout << prefix << "host_native_atomic_supported=" << yes_no(properties.hostNativeAtomicSupported) << '\n';
        std::cout << prefix << "cooperative_launch=" << yes_no(properties.cooperativeLaunch) << '\n';
        std::cout << prefix << "memory_pools_supported=" << yes_no(properties.memoryPoolsSupported) << '\n';
        std::cout << prefix << "direct_managed_memory_access_from_host="
                  << yes_no(properties.directManagedMemAccessFromHost) << '\n';
    }
    return 0;
}
