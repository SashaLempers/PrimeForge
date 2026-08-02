// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/system_info.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <intrin.h>
#include <windows.h>
#include <winternl.h>
#elif defined(__linux__)
#include <cpuid.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace primeforge {
namespace {

struct CpuidRegisters {
    unsigned int eax{};
    unsigned int ebx{};
    unsigned int ecx{};
    unsigned int edx{};
};

[[nodiscard]] CpuidRegisters cpuid(const unsigned int leaf, const unsigned int subleaf = 0U) noexcept {
    CpuidRegisters registers{};
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    std::array<int, 4> values{};
    __cpuidex(values.data(), static_cast<int>(leaf), static_cast<int>(subleaf));
    registers.eax = static_cast<unsigned int>(values[0]);
    registers.ebx = static_cast<unsigned int>(values[1]);
    registers.ecx = static_cast<unsigned int>(values[2]);
    registers.edx = static_cast<unsigned int>(values[3]);
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    __get_cpuid_count(leaf, subleaf, &registers.eax, &registers.ebx, &registers.ecx, &registers.edx);
#else
    static_cast<void>(leaf);
    static_cast<void>(subleaf);
#endif
    return registers;
}

[[nodiscard]] std::uint64_t xgetbv_zero() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    return _xgetbv(0);
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    unsigned int eax{};
    unsigned int edx{};
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<std::uint64_t>(edx) << 32U) | eax;
#else
    return 0U;
#endif
}

[[nodiscard]] std::string cpu_brand() {
    const auto maximum = cpuid(0x80000000U).eax;
    if (maximum < 0x80000004U) {
        return "UNKNOWN";
    }

    std::array<unsigned int, 12> words{};
    for (unsigned int index = 0; index < 3U; ++index) {
        const auto registers = cpuid(0x80000002U + index);
        words[index * 4U] = registers.eax;
        words[index * 4U + 1U] = registers.ebx;
        words[index * 4U + 2U] = registers.ecx;
        words[index * 4U + 3U] = registers.edx;
    }

    std::array<char, 49> text{};
    std::memcpy(text.data(), words.data(), 48U);
    std::string result{text.data()};
    const auto first = result.find_first_not_of(' ');
    const auto last = result.find_last_not_of(' ');
    if (first == std::string::npos) {
        return "UNKNOWN";
    }
    return result.substr(first, last - first + 1U);
}

[[nodiscard]] std::string architecture_name() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#else
    return "UNKNOWN";
#endif
}

[[nodiscard]] CompilerInfo compiler_info() {
    CompilerInfo info{};
#if defined(_MSC_VER)
    info.name = "MSVC";
    info.version = std::to_string(_MSC_FULL_VER);
    info.msc_ver = _MSC_VER;
    info.msc_full_ver = _MSC_FULL_VER;
#elif defined(__clang__)
    info.name = "Clang";
    info.version = __clang_version__;
#elif defined(__GNUC__)
    info.name = "GCC";
    info.version = __VERSION__;
#else
    info.name = "UNKNOWN";
    info.version = "UNKNOWN";
#endif
    info.cplusplus = __cplusplus;
    return info;
}

#if defined(_WIN32)
[[nodiscard]] std::string wide_to_utf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), required, nullptr, nullptr);
    result.pop_back();
    return result;
}

[[nodiscard]] unsigned int windows_physical_cores() {
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (length == 0U) {
        return 0U;
    }
    std::vector<std::byte> buffer(length);
    if (GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
            &length) == FALSE) {
        return 0U;
    }

    unsigned int count = 0U;
    std::size_t offset = 0U;
    while (offset < length) {
        const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (entry->Relationship == RelationProcessorCore) {
            ++count;
        }
        if (entry->Size == 0U) {
            break;
        }
        offset += entry->Size;
    }
    return count;
}
#endif

[[nodiscard]] OperatingSystemInfo operating_system_info() {
    OperatingSystemInfo info{};
    info.architecture = architecture_name();
#if defined(_WIN32)
    info.name = "Windows";
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto function = module == nullptr
        ? nullptr
        : reinterpret_cast<NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW)>(GetProcAddress(module, "RtlGetVersion"));
    if (function != nullptr && function(&version) == 0) {
        info.version = std::to_string(version.dwMajorVersion) + "." +
            std::to_string(version.dwMinorVersion) + "." + std::to_string(version.dwBuildNumber);
    } else {
        info.version = "UNKNOWN";
    }
#elif defined(__linux__)
    info.name = "Linux";
    utsname details{};
    if (uname(&details) == 0) {
        info.version = details.release;
    } else {
        info.version = "UNKNOWN";
    }
#else
    info.name = "UNKNOWN";
    info.version = "UNKNOWN";
#endif
    return info;
}

[[nodiscard]] CpuCapabilities cpu_capabilities() {
    CpuCapabilities capabilities{};
    capabilities.brand = cpu_brand();
    capabilities.logical_cores = std::thread::hardware_concurrency();
#if defined(_WIN32)
    capabilities.physical_cores = windows_physical_cores();
#elif defined(__linux__)
    const long configured = sysconf(_SC_NPROCESSORS_CONF);
    capabilities.physical_cores = configured > 0L ? static_cast<unsigned int>(configured) : 0U;
#endif

    const auto basic_max = cpuid(0U).eax;
    if (basic_max >= 1U) {
        const auto leaf_one = cpuid(1U);
        capabilities.sse2 = (leaf_one.edx & (1U << 26U)) != 0U;
        const bool osxsave = (leaf_one.ecx & (1U << 27U)) != 0U;
        const bool hardware_avx = (leaf_one.ecx & (1U << 28U)) != 0U;
        const std::uint64_t xcr0 = osxsave ? xgetbv_zero() : 0U;
        const bool os_avx = (xcr0 & 0x6U) == 0x6U;
        capabilities.avx = hardware_avx && os_avx;

        if (basic_max >= 7U) {
            const auto leaf_seven = cpuid(7U, 0U);
            capabilities.avx2 = capabilities.avx && (leaf_seven.ebx & (1U << 5U)) != 0U;
            capabilities.bmi2 = (leaf_seven.ebx & (1U << 8U)) != 0U;
            const bool os_avx512 = (xcr0 & 0xe6U) == 0xe6U;
            capabilities.avx512f = os_avx512 && (leaf_seven.ebx & (1U << 16U)) != 0U;
        }
    }
    return capabilities;
}

[[nodiscard]] GpuInfo gpu_info() {
    GpuInfo info{};
#if defined(_WIN32)
    for (DWORD index = 0U;; ++index) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (EnumDisplayDevicesW(nullptr, index, &device, 0U) == FALSE) {
            break;
        }
        if ((device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0U) {
            continue;
        }
        auto name = wide_to_utf8(device.DeviceString);
        if (!name.empty() && std::find(info.adapters.begin(), info.adapters.end(), name) == info.adapters.end()) {
            info.adapters.push_back(std::move(name));
        }
    }
#endif
    info.available = !info.adapters.empty();
    return info;
}

[[nodiscard]] std::string yes_no(const bool value) {
    return value ? "yes" : "no";
}

} // namespace

SystemInfo collect_system_info() {
    SystemInfo info{};
    info.compiler = compiler_info();
    info.operating_system = operating_system_info();
    info.cpu = cpu_capabilities();
    info.gpu = gpu_info();
    info.electrical_power_watts = "UNKNOWN";
    return info;
}

std::string format_system_info(const SystemInfo& info) {
    std::ostringstream output;
    output << "compiler.name=" << info.compiler.name << '\n';
    output << "compiler.version=" << info.compiler.version << '\n';
    output << "compiler._MSC_VER=" << info.compiler.msc_ver << '\n';
    output << "compiler._MSC_FULL_VER=" << info.compiler.msc_full_ver << '\n';
    output << "compiler.__cplusplus=" << info.compiler.cplusplus << '\n';
    output << "compiler.language_mode="
           << (info.compiler.cplusplus >= 202100L ? "C++23" : "PRE_C++23") << '\n';
    output << "os.name=" << info.operating_system.name << '\n';
    output << "os.version=" << info.operating_system.version << '\n';
    output << "os.architecture=" << info.operating_system.architecture << '\n';
    output << "cpu.brand=" << info.cpu.brand << '\n';
    output << "cpu.physical_cores=" << info.cpu.physical_cores << '\n';
    output << "cpu.logical_cores=" << info.cpu.logical_cores << '\n';
    output << "cpu.sse2=" << yes_no(info.cpu.sse2) << '\n';
    output << "cpu.avx=" << yes_no(info.cpu.avx) << '\n';
    output << "cpu.avx2=" << yes_no(info.cpu.avx2) << '\n';
    output << "cpu.avx512f=" << yes_no(info.cpu.avx512f) << '\n';
    output << "cpu.bmi2=" << yes_no(info.cpu.bmi2) << '\n';
    output << "gpu.available=" << yes_no(info.gpu.available) << '\n';
    for (std::size_t index = 0; index < info.gpu.adapters.size(); ++index) {
        output << "gpu.adapter." << index << '=' << info.gpu.adapters[index] << '\n';
    }
    output << "electrical_power_watts=" << info.electrical_power_watts << '\n';
    return output.str();
}

} // namespace primeforge
