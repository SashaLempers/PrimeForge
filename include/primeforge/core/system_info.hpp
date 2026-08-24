// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace primeforge {

struct CompilerInfo {
    std::string name;
    std::string version;
    std::uint64_t msc_ver{};
    std::uint64_t msc_full_ver{};
    std::uint64_t cplusplus{};
};

struct OperatingSystemInfo {
    std::string name;
    std::string version;
    std::string architecture;
};

struct CpuCapabilities {
    std::string brand;
    unsigned int physical_cores{};
    unsigned int logical_cores{};
    bool sse2{};
    bool avx{};
    bool avx2{};
    bool avx512f{};
    bool avx512ifma{};
    bool bmi2{};
};

struct GpuInfo {
    bool available{};
    std::vector<std::string> adapters;
};

struct SystemInfo {
    CompilerInfo compiler;
    OperatingSystemInfo operating_system;
    CpuCapabilities cpu;
    GpuInfo gpu;
    std::string electrical_power_watts{"UNKNOWN"};
};

[[nodiscard]] SystemInfo collect_system_info();
[[nodiscard]] CpuCapabilities collect_cpu_capabilities();
[[nodiscard]] std::string format_system_info(const SystemInfo& info);

} // namespace primeforge
