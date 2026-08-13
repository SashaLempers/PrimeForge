// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/benchmark_watchdog.hpp"

#include "primeforge/core/sha256.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"
#include "primeforge/work/work_unit.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace {
std::atomic_bool signal_stop{false};
void on_signal(int) { signal_stop.store(true); }

[[nodiscard]] std::string tsv_value(std::string value) {
    for (char& character : value) {
        if (character == '\t' || character == '\r' || character == '\n') { character = ' '; }
    }
    return value;
}

[[nodiscard]] std::optional<double> metric_number(const primeforge::runtime::Metric& metric) {
    if (!metric.available()) { return std::nullopt; }
    try {
        std::size_t parsed = 0U;
        const double value = std::stod(metric.value, &parsed);
        if (parsed == metric.value.size() && std::isfinite(value)) { return value; }
    } catch (const std::exception&) {}
    return std::nullopt;
}

[[nodiscard]] std::string number(const double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6) << value;
    return output.str();
}

struct ProcessMetrics {
    std::string process_cpu_percent{"UNKNOWN"};
    std::string process_cpu_time_ms{"UNKNOWN"};
    std::string system_cpu_percent{"UNKNOWN"};
    std::string process_working_set_bytes{"UNKNOWN"};
    std::string process_private_bytes{"UNKNOWN"};
    std::string process_read_bytes{"UNKNOWN"};
    std::string process_write_bytes{"UNKNOWN"};
};

#ifdef _WIN32
[[nodiscard]] std::uint64_t file_time(const FILETIME& value) {
    ULARGE_INTEGER integer{};
    integer.LowPart = value.dwLowDateTime;
    integer.HighPart = value.dwHighDateTime;
    return integer.QuadPart;
}

class ProcessSampler {
public:
    explicit ProcessSampler(const std::uint64_t pid)
        : handle_(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid))) {}
    ~ProcessSampler() { if (handle_ != nullptr) { CloseHandle(handle_); } }
    ProcessSampler(const ProcessSampler&) = delete;
    ProcessSampler& operator=(const ProcessSampler&) = delete;

    [[nodiscard]] ProcessMetrics sample() {
        ProcessMetrics result;
        if (handle_ == nullptr) { return result; }
        FILETIME creation{}, exit{}, kernel{}, user{};
        FILETIME idle_system{}, kernel_system{}, user_system{};
        if (GetProcessTimes(handle_, &creation, &exit, &kernel, &user) != 0) {
            const auto process_total = file_time(kernel) + file_time(user);
            result.process_cpu_time_ms = std::to_string(process_total / 10'000ULL);
            if (GetSystemTimes(&idle_system, &kernel_system, &user_system) != 0) {
                const auto idle_total = file_time(idle_system);
                const auto system_total = file_time(kernel_system) + file_time(user_system);
                if (previous_system_total_ != 0U && system_total > previous_system_total_ &&
                    process_total >= previous_process_total_ && idle_total >= previous_idle_total_) {
                    const auto system_delta = system_total - previous_system_total_;
                    const auto idle_delta = idle_total - previous_idle_total_;
                    const auto process_delta = process_total - previous_process_total_;
                    const auto bounded_idle_delta = idle_delta < system_delta ? idle_delta : system_delta;
                    result.system_cpu_percent = number(100.0 *
                        static_cast<double>(system_delta - bounded_idle_delta) /
                        static_cast<double>(system_delta));
                    result.process_cpu_percent = number(100.0 * static_cast<double>(process_delta) /
                        static_cast<double>(system_delta));
                }
                previous_idle_total_ = idle_total;
                previous_system_total_ = system_total;
                previous_process_total_ = process_total;
            }
        }
        PROCESS_MEMORY_COUNTERS_EX memory{};
        if (GetProcessMemoryInfo(
                handle_, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != 0) {
            result.process_working_set_bytes = std::to_string(memory.WorkingSetSize);
            result.process_private_bytes = std::to_string(memory.PrivateUsage);
        }
        IO_COUNTERS io{};
        if (GetProcessIoCounters(handle_, &io) != 0) {
            result.process_read_bytes = std::to_string(io.ReadTransferCount);
            result.process_write_bytes = std::to_string(io.WriteTransferCount);
        }
        return result;
    }

private:
    HANDLE handle_{};
    std::uint64_t previous_idle_total_{};
    std::uint64_t previous_system_total_{};
    std::uint64_t previous_process_total_{};
};
#else
class ProcessSampler {
public:
    explicit ProcessSampler(std::uint64_t) {}
    [[nodiscard]] ProcessMetrics sample() const { return {}; }
};
#endif

void write_compact_status(
    const std::filesystem::path& path,
    const std::uint64_t pid,
    const primeforge::runtime::HardwareSnapshot& snapshot,
    const primeforge::runtime::WatchdogOutcome& outcome,
    const ProcessMetrics& process,
    const double gpu_energy_wh) {
    if (path.empty()) { return; }
    const auto mib_bytes = [](const primeforge::runtime::Metric& metric) {
        const auto value = metric_number(metric);
        return value ? std::to_string(static_cast<std::uint64_t>(*value * 1024.0 * 1024.0)) : std::string{"UNKNOWN"};
    };
    const std::map<std::string, std::string, std::less<>> values{
        {"schema_version", "1"}, {"utc", snapshot.utc},
        {"monotonic_ms", std::to_string(snapshot.monotonic_milliseconds)},
        {"decision", primeforge::runtime::to_string(outcome.decision)}, {"reason", outcome.reason},
        {"performance_valid", outcome.performance_valid ? "true" : "false"}, {"worker_pid", std::to_string(pid)},
        {"gpu_name", snapshot.gpu_name.value}, {"gpu_uuid", snapshot.gpu_uuid.value},
        {"gpu_util_percent", snapshot.gpu_utilization_percent.value},
        {"gpu_memory_controller_percent", snapshot.gpu_memory_utilization_percent.value},
        {"gpu_power_w", snapshot.gpu_power_watts.value}, {"gpu_energy_wh_integrated", number(gpu_energy_wh)},
        {"gpu_temperature_c", snapshot.gpu_temperature_celsius.value}, {"gpu_hotspot_c", "UNKNOWN"},
        {"gpu_memory_temperature_c", snapshot.gpu_memory_temperature_celsius.value},
        {"gpu_core_clock_mhz", snapshot.gpu_sm_clock_mhz.value},
        {"gpu_memory_clock_mhz", snapshot.gpu_memory_clock_mhz.value},
        {"gpu_vram_used_bytes", mib_bytes(snapshot.vram_used_mib)},
        {"gpu_vram_total_bytes", mib_bytes(snapshot.vram_total_mib)},
        {"gpu_pcie_tx_bytes_per_s", "UNKNOWN"}, {"gpu_pcie_rx_bytes_per_s", "UNKNOWN"},
        {"gpu_throttle_reasons", snapshot.throttling_reasons},
        {"process_cpu_percent", process.process_cpu_percent},
        {"process_cpu_time_ms", process.process_cpu_time_ms}, {"system_cpu_percent", process.system_cpu_percent},
        {"scheduler_thread_cpu_ms", "UNKNOWN"}, {"process_working_set_bytes", process.process_working_set_bytes},
        {"process_private_bytes", process.process_private_bytes},
        {"system_available_ram_bytes", snapshot.ram_available_bytes.value},
        {"process_read_bytes", process.process_read_bytes}, {"process_write_bytes", process.process_write_bytes},
        {"whea_delta", snapshot.whea_errors_recent.value}, {"xid_delta", snapshot.nvidia_xid_errors_recent.value}};
    std::string content;
    for (auto iterator = values.begin(); iterator != values.end(); ++iterator) {
        if (iterator != values.begin()) { content.push_back('\t'); }
        content += iterator->first + "=" + tsv_value(iterator->second);
    }
    content.push_back('\n');
    primeforge::work::write_checkpoint_atomically(path, content);
}
}

int main(int argc, char** argv) {
    try {
        std::uint64_t pid{};
        std::uint64_t interval_ms{1'000U};
        primeforge::runtime::WatchdogPolicy policy;
        std::string stop_file;
        std::string watchdog_stop_file;
        std::string log_path;
        std::string campaign;
        std::string checkpoint_path;
        std::string compact_status_path;
        bool detailed_log_enabled = true;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--pid" && index + 1 < argc) { pid = std::stoull(argv[++index]); }
            else if (argument == "--stop-file" && index + 1 < argc) { stop_file = argv[++index]; }
            else if (argument == "--watchdog-stop-file" && index + 1 < argc) { watchdog_stop_file = argv[++index]; }
            else if (argument == "--log" && index + 1 < argc) { log_path = argv[++index]; }
            else if (argument == "--campaign" && index + 1 < argc) { campaign = argv[++index]; }
            else if (argument == "--checkpoint" && index + 1 < argc) { checkpoint_path = argv[++index]; }
            else if (argument == "--compact-status-file" && index + 1 < argc) { compact_status_path = argv[++index]; }
            else if (argument == "--disable-detailed-log") { detailed_log_enabled = false; }
            else if (argument == "--interval-ms" && index + 1 < argc) { interval_ms = std::stoull(argv[++index]); }
            else if (argument == "--grace-ms" && index + 1 < argc) { policy.graceful_timeout_milliseconds = std::stoull(argv[++index]); }
            else if (argument == "--max-cpu-temp-c" && index + 1 < argc) { policy.maximum_cpu_temperature_celsius = std::stod(argv[++index]); }
            else if (argument == "--max-cpu-power-w" && index + 1 < argc) { policy.maximum_cpu_power_watts = std::stod(argv[++index]); }
            else if (argument == "--max-gpu-temp-c" && index + 1 < argc) { policy.maximum_gpu_temperature_celsius = std::stod(argv[++index]); }
            else if (argument == "--max-gpu-power-w" && index + 1 < argc) { policy.maximum_gpu_power_watts = std::stod(argv[++index]); }
            else if (argument == "--min-ram-available-bytes" && index + 1 < argc) { policy.minimum_ram_available_bytes = std::stoull(argv[++index]); }
            else if (argument == "--min-vram-free-mib" && index + 1 < argc) { policy.minimum_vram_free_mib = std::stod(argv[++index]); }
            else if (argument == "--require-cpu-temperature") { policy.require_cpu_temperature = true; }
            else if (argument == "--require-cpu-power") { policy.require_cpu_power = true; }
            else if (argument == "--require-gpu-temperature") { policy.require_gpu_temperature = true; }
            else if (argument == "--require-gpu-power") { policy.require_gpu_power = true; }
            else if (argument == "--require-ram-available") { policy.require_ram_available = true; }
            else if (argument == "--require-vram-free") { policy.require_vram_free = true; }
            else if (argument == "--require-whea-status") { policy.require_whea_status = true; }
            else if (argument == "--require-nvidia-xid-status") { policy.require_nvidia_xid_status = true; }
            else if (argument == "--fatal-sw-power-cap") { policy.software_power_cap_is_fatal = true; }
            else { throw std::invalid_argument("invalid benchmark_watchdog argument"); }
        }
        if (pid == 0U || interval_ms == 0U || stop_file.empty() || campaign.empty() ||
            (detailed_log_enabled && log_path.empty())) {
            throw std::invalid_argument("--pid, --stop-file, --campaign, positive --interval-ms and an enabled --log are required");
        }

        primeforge::runtime::BenchmarkLogger logger(log_path, campaign, detailed_log_enabled);
        if (!checkpoint_path.empty()) {
            primeforge::PortableSha256Provider sha256;
            primeforge::runtime::CheckpointManager checkpoints(sha256);
            const auto checkpoint = checkpoints.load(checkpoint_path);
            logger.append(
                "checkpoint_loaded",
                "{\"progress_decimal\":\"" + checkpoint.progress_decimal +
                    "\",\"sequence\":\"" + std::to_string(checkpoint.sequence) + "\"}",
                primeforge::runtime::utc_now());
        }

        primeforge::runtime::ProcessWorkerController worker(pid, stop_file);
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        primeforge::runtime::HardwareMonitor monitor;
        ProcessSampler process_sampler(pid);
        double gpu_energy_wh = 0.0;
        std::optional<double> previous_gpu_power;
        std::optional<std::uint64_t> previous_monotonic_ms;
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        while (true) {
            const auto snapshot = monitor.sample();
            const bool external_stop = signal_stop.load() ||
                (!watchdog_stop_file.empty() && std::filesystem::exists(watchdog_stop_file));
            const auto outcome = watchdog.tick(snapshot.monotonic_milliseconds, snapshot, external_stop);
            const auto power = metric_number(snapshot.gpu_power_watts);
            if (power && previous_gpu_power && previous_monotonic_ms &&
                snapshot.monotonic_milliseconds >= *previous_monotonic_ms) {
                const auto hours = static_cast<double>(snapshot.monotonic_milliseconds - *previous_monotonic_ms) /
                    3'600'000.0;
                gpu_energy_wh += ((*previous_gpu_power + *power) * 0.5) * hours;
            }
            previous_gpu_power = power;
            previous_monotonic_ms = snapshot.monotonic_milliseconds;
            write_compact_status(
                compact_status_path, pid, snapshot, outcome, process_sampler.sample(), gpu_energy_wh);
            std::cout << "benchmark_watchdog.decision=" << primeforge::runtime::to_string(outcome.decision)
                      << " reason=" << outcome.reason << '\n' << std::flush;
            if (outcome.decision == primeforge::runtime::WatchdogDecision::forced_stop ||
                outcome.decision == primeforge::runtime::WatchdogDecision::worker_exited) {
                return 0;
            }
            std::uint64_t waited{};
            while (waited < interval_ms && worker.alive() && !signal_stop.load() &&
                   (watchdog_stop_file.empty() || !std::filesystem::exists(watchdog_stop_file))) {
                const auto slice = std::min<std::uint64_t>(100U, interval_ms - waited);
                std::this_thread::sleep_for(std::chrono::milliseconds(slice));
                waited += slice;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "benchmark_watchdog: " << error.what() << '\n';
        return 1;
    }
}
