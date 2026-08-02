// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/hardware_monitor.hpp"

#include "primeforge/runtime/benchmark_logger.hpp"
#include "runtime_internal.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <powrprof.h>
#else
#include <sys/wait.h>
#endif

namespace primeforge::runtime {
namespace {

[[nodiscard]] Metric unknown_metric(const std::string& source) {
    return Metric{source, "UNKNOWN"};
}

[[nodiscard]] Metric detected_metric(const std::string& source, std::string value) {
    value = internal::trim(std::move(value));
    if (value.empty() || value == "N/A" || value == "[Not Supported]") {
        return unknown_metric(source);
    }
    return Metric{source, std::move(value)};
}

[[nodiscard]] std::string metric_json(const Metric& metric) {
    return "{\"source\":" + internal::json_escape(metric.source) +
           ",\"status\":\"" + (metric.available() ? "DETECTED" : "UNKNOWN") +
           "\",\"value\":" + internal::json_escape(metric.value) + "}";
}

[[nodiscard]] std::optional<std::string> run_command(const std::string_view command) {
#ifdef _WIN32
    std::FILE* pipe = _popen(std::string(command).c_str(), "r");
#else
    std::FILE* pipe = popen(std::string(command).c_str(), "r");
#endif
    if (pipe == nullptr) {
        return std::nullopt;
    }
    std::string output;
    std::array<char, 1024> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
#ifdef _WIN32
    const int status = _pclose(pipe);
    if (status != 0) {
#else
    const int raw_status = pclose(pipe);
    if (raw_status == -1 || !WIFEXITED(raw_status) || WEXITSTATUS(raw_status) != 0) {
#endif
        return std::nullopt;
    }
    return output;
}

void collect_cpu_and_ram(HardwareSnapshot& snapshot) {
#ifdef _WIN32
    struct ProcessorPowerInformation {
        ULONG number;
        ULONG maximum_mhz;
        ULONG current_mhz;
        ULONG mhz_limit;
        ULONG maximum_idle_state;
        ULONG current_idle_state;
    };
    SYSTEM_INFO system_info{};
    GetNativeSystemInfo(&system_info);
    std::vector<ProcessorPowerInformation> information(system_info.dwNumberOfProcessors);
    const auto length = static_cast<ULONG>(information.size() * sizeof(ProcessorPowerInformation));
    if (!information.empty() &&
        CallNtPowerInformation(ProcessorInformation, nullptr, 0U, information.data(), length) == ERROR_SUCCESS) {
        std::uint64_t total = 0U;
        for (const auto& processor : information) {
            total += processor.current_mhz;
        }
        snapshot.cpu_frequency_mhz = detected_metric(
            "CallNtPowerInformation.ProcessorInformation.mean_CurrentMhz",
            std::to_string(total / information.size()));
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) != 0) {
        snapshot.ram_available_bytes = detected_metric(
            "GlobalMemoryStatusEx.ullAvailPhys", std::to_string(memory.ullAvailPhys));
        snapshot.ram_used_bytes = detected_metric(
            "GlobalMemoryStatusEx.total-minus-available",
            std::to_string(memory.ullTotalPhys - memory.ullAvailPhys));
    }
#else
    std::ifstream frequency("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    std::uint64_t frequency_khz{};
    if (frequency >> frequency_khz) {
        snapshot.cpu_frequency_mhz = detected_metric(
            "sysfs.cpu0.scaling_cur_freq", std::to_string(frequency_khz / 1000U));
    }

    std::ifstream memory("/proc/meminfo");
    std::string key;
    std::uint64_t value_kib{};
    std::string unit;
    std::uint64_t total_kib{};
    std::uint64_t available_kib{};
    while (memory >> key >> value_kib >> unit) {
        if (key == "MemTotal:") { total_kib = value_kib; }
        if (key == "MemAvailable:") { available_kib = value_kib; }
    }
    if (total_kib != 0U && available_kib != 0U && total_kib >= available_kib) {
        snapshot.ram_available_bytes = detected_metric(
            "proc.meminfo.MemAvailable", std::to_string(available_kib * 1024U));
        snapshot.ram_used_bytes = detected_metric(
            "proc.meminfo.total-minus-available", std::to_string((total_kib - available_kib) * 1024U));
    }
#endif
}

void collect_nvidia(HardwareSnapshot& snapshot, const CommandRunner& runner) {
    constexpr std::string_view command =
        "nvidia-smi --query-gpu=temperature.gpu,temperature.memory,power.draw,power.limit,"
        "clocks.sm,clocks.mem,utilization.gpu,utilization.memory,memory.used,memory.free,"
        "clocks_event_reasons.sw_power_cap,clocks_event_reasons.sw_thermal_slowdown,"
        "clocks_event_reasons.hw_thermal_slowdown,clocks_event_reasons.hw_power_brake_slowdown "
        "--format=csv,noheader,nounits";
    const auto output = runner(command);
    if (!output) {
        return;
    }
    const auto newline = output->find_first_of("\r\n");
    const auto fields = internal::split_csv(output->substr(0U, newline));
    if (fields.size() != 14U) {
        return;
    }
    snapshot.gpu_temperature_celsius = detected_metric("nvidia-smi.temperature.gpu", fields[0]);
    snapshot.gpu_memory_temperature_celsius = detected_metric("nvidia-smi.temperature.memory", fields[1]);
    snapshot.gpu_power_watts = detected_metric("nvidia-smi.power.draw", fields[2]);
    snapshot.gpu_power_limit_watts = detected_metric("nvidia-smi.power.limit", fields[3]);
    snapshot.gpu_sm_clock_mhz = detected_metric("nvidia-smi.clocks.sm", fields[4]);
    snapshot.gpu_memory_clock_mhz = detected_metric("nvidia-smi.clocks.mem", fields[5]);
    snapshot.gpu_utilization_percent = detected_metric("nvidia-smi.utilization.gpu", fields[6]);
    snapshot.gpu_memory_utilization_percent = detected_metric("nvidia-smi.utilization.memory", fields[7]);
    snapshot.vram_used_mib = detected_metric("nvidia-smi.memory.used", fields[8]);
    snapshot.vram_free_mib = detected_metric("nvidia-smi.memory.free", fields[9]);

    constexpr std::array<std::string_view, 4> reason_names{
        "SW_POWER_CAP", "SW_THERMAL_SLOWDOWN", "HW_THERMAL_SLOWDOWN", "HW_POWER_BRAKE_SLOWDOWN"};
    std::string active_reasons;
    bool reason_available = false;
    for (std::size_t index = 0U; index < reason_names.size(); ++index) {
        const auto& value = fields[index + 10U];
        if (value != "N/A" && value != "[Not Supported]") {
            reason_available = true;
        }
        if (value == "Active") {
            if (!active_reasons.empty()) { active_reasons += ','; }
            active_reasons += reason_names[index];
        }
    }
    snapshot.throttling_detected = !active_reasons.empty();
    snapshot.throttling_reasons = snapshot.throttling_detected
                                      ? active_reasons
                                      : (reason_available ? "NONE" : "UNKNOWN");
}

} // namespace

std::string HardwareSnapshot::canonical_json() const {
    return "{\"cpu_frequency_mhz\":" + metric_json(cpu_frequency_mhz) +
           ",\"cpu_power_watts\":" + metric_json(cpu_power_watts) +
           ",\"cpu_temperature_celsius\":" + metric_json(cpu_temperature_celsius) +
           ",\"gpu_memory_clock_mhz\":" + metric_json(gpu_memory_clock_mhz) +
           ",\"gpu_memory_temperature_celsius\":" + metric_json(gpu_memory_temperature_celsius) +
           ",\"gpu_memory_utilization_percent\":" + metric_json(gpu_memory_utilization_percent) +
           ",\"gpu_power_limit_watts\":" + metric_json(gpu_power_limit_watts) +
           ",\"gpu_power_watts\":" + metric_json(gpu_power_watts) +
           ",\"gpu_sm_clock_mhz\":" + metric_json(gpu_sm_clock_mhz) +
           ",\"gpu_temperature_celsius\":" + metric_json(gpu_temperature_celsius) +
           ",\"gpu_utilization_percent\":" + metric_json(gpu_utilization_percent) +
           ",\"monotonic_milliseconds\":" + internal::json_escape(std::to_string(monotonic_milliseconds)) +
           ",\"ram_available_bytes\":" + metric_json(ram_available_bytes) +
           ",\"ram_used_bytes\":" + metric_json(ram_used_bytes) +
           ",\"throttling_detected\":" + (throttling_detected ? "true" : "false") +
           ",\"throttling_reasons\":" + internal::json_escape(throttling_reasons) +
           ",\"utc\":" + internal::json_escape(utc) +
           ",\"vram_free_mib\":" + metric_json(vram_free_mib) +
           ",\"vram_used_mib\":" + metric_json(vram_used_mib) + "}";
}

HardwareMonitor::HardwareMonitor(CommandRunner runner)
    : runner_(runner ? std::move(runner) : CommandRunner{run_command}) {}

HardwareSnapshot HardwareMonitor::sample() const {
    HardwareSnapshot snapshot;
    snapshot.utc = utc_now();
    snapshot.monotonic_milliseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    snapshot.cpu_frequency_mhz = unknown_metric("no validated CPU frequency provider");
    snapshot.cpu_temperature_celsius = unknown_metric("no validated CPU temperature provider");
    snapshot.cpu_power_watts = unknown_metric("no validated CPU power provider");
    snapshot.ram_used_bytes = unknown_metric("no RAM provider");
    snapshot.ram_available_bytes = unknown_metric("no RAM provider");
    snapshot.gpu_temperature_celsius = unknown_metric("nvidia-smi.temperature.gpu");
    snapshot.gpu_memory_temperature_celsius = unknown_metric("nvidia-smi.temperature.memory");
    snapshot.gpu_power_watts = unknown_metric("nvidia-smi.power.draw");
    snapshot.gpu_power_limit_watts = unknown_metric("nvidia-smi.power.limit");
    snapshot.gpu_sm_clock_mhz = unknown_metric("nvidia-smi.clocks.sm");
    snapshot.gpu_memory_clock_mhz = unknown_metric("nvidia-smi.clocks.mem");
    snapshot.gpu_utilization_percent = unknown_metric("nvidia-smi.utilization.gpu");
    snapshot.gpu_memory_utilization_percent = unknown_metric("nvidia-smi.utilization.memory");
    snapshot.vram_used_mib = unknown_metric("nvidia-smi.memory.used");
    snapshot.vram_free_mib = unknown_metric("nvidia-smi.memory.free");
    collect_cpu_and_ram(snapshot);
    collect_nvidia(snapshot, runner_);
    return snapshot;
}

} // namespace primeforge::runtime
