// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/benchmark_watchdog.hpp"

#include "primeforge/work/work_unit.hpp"
#include "runtime_internal.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#endif

namespace primeforge::runtime {
namespace {

[[nodiscard]] std::optional<double> metric_value(const Metric& metric) {
    if (!metric.available()) {
        return std::nullopt;
    }
    const auto value = internal::parse_double(metric.value);
    return value && std::isfinite(*value) ? value : std::nullopt;
}

void validate_threshold(const std::optional<double>& threshold, const char* name) {
    if (threshold && (!std::isfinite(*threshold) || *threshold <= 0.0)) {
        throw std::invalid_argument(std::string{name} + " must be finite and positive");
    }
}

} // namespace

ProcessWorkerController::ProcessWorkerController(
    const std::uint64_t process_id,
    std::filesystem::path stop_file)
    : process_id_(process_id), stop_file_(std::move(stop_file)) {
    if (process_id_ == 0U || !stop_file_.has_filename()) {
        throw std::invalid_argument("worker pid and stop-file path are required");
    }
#ifdef _WIN32
    process_handle_ = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
        FALSE,
        static_cast<DWORD>(process_id_));
    if (process_handle_ == nullptr) {
        throw std::runtime_error("cannot open worker process");
    }
#endif
}

ProcessWorkerController::~ProcessWorkerController() {
#ifdef _WIN32
    if (process_handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(process_handle_));
    }
#endif
}

bool ProcessWorkerController::alive() const {
#ifdef _WIN32
    return process_handle_ != nullptr &&
        WaitForSingleObject(static_cast<HANDLE>(process_handle_), 0U) == WAIT_TIMEOUT;
#else
    const int result = kill(static_cast<pid_t>(process_id_), 0);
    return result == 0 || errno == EPERM;
#endif
}

std::optional<int> ProcessWorkerController::exit_code() const {
#ifdef _WIN32
    DWORD code = STILL_ACTIVE;
    if (process_handle_ == nullptr ||
        GetExitCodeProcess(static_cast<HANDLE>(process_handle_), &code) == 0 ||
        code == STILL_ACTIVE) {
        return std::nullopt;
    }
    return static_cast<int>(code);
#else
    return std::nullopt;
#endif
}

void ProcessWorkerController::request_graceful_stop() {
    if (!stop_file_.parent_path().empty()) {
        std::filesystem::create_directories(stop_file_.parent_path());
    }
    work::write_checkpoint_atomically(stop_file_, "STOP\n");
}

void ProcessWorkerController::force_stop() {
#ifdef _WIN32
    const auto process = static_cast<HANDLE>(process_handle_);
    if (process == nullptr) {
        if (alive()) {
            throw std::runtime_error("cannot open worker for forced termination");
        }
        return;
    }
    if (TerminateProcess(process, 3U) == 0) {
        throw std::runtime_error("cannot force-stop worker");
    }
    static_cast<void>(WaitForSingleObject(process, 5'000U));
#else
    if (kill(static_cast<pid_t>(process_id_), SIGKILL) != 0 && errno != ESRCH) {
        throw std::runtime_error("cannot force-stop worker");
    }
#endif
}

BenchmarkWatchdog::BenchmarkWatchdog(
    WatchdogPolicy policy,
    WorkerController& worker,
    BenchmarkLogger& logger)
    : policy_(std::move(policy)), worker_(worker), logger_(logger) {
    if (policy_.graceful_timeout_milliseconds == 0U) {
        throw std::invalid_argument("watchdog graceful timeout must be positive");
    }
    validate_threshold(policy_.maximum_cpu_temperature_celsius, "maximum CPU temperature");
    validate_threshold(policy_.maximum_cpu_power_watts, "maximum CPU power");
    validate_threshold(policy_.maximum_gpu_temperature_celsius, "maximum GPU temperature");
    validate_threshold(policy_.maximum_gpu_power_watts, "maximum GPU power");
    if (policy_.minimum_ram_available_bytes && *policy_.minimum_ram_available_bytes == 0U) {
        throw std::invalid_argument("minimum available RAM must be positive");
    }
    validate_threshold(policy_.minimum_vram_free_mib, "minimum free VRAM");
}

std::optional<std::string> BenchmarkWatchdog::unsafe_reason(
    const HardwareSnapshot& snapshot) const {
    if (snapshot.throttling_detected) {
        const auto reasons = internal::split_csv(snapshot.throttling_reasons);
        const bool software_power_cap_only = !reasons.empty() &&
            std::ranges::all_of(reasons, [](const std::string& reason) {
                return reason == "SW_POWER_CAP";
            });
        if (!software_power_cap_only || policy_.software_power_cap_is_fatal) {
            return "GPU_THROTTLING:" + snapshot.throttling_reasons;
        }
    }
    const auto cpu_temperature = metric_value(snapshot.cpu_temperature_celsius);
    if (policy_.require_cpu_temperature && !cpu_temperature) {
        return "CPU_TEMPERATURE_SENSOR_LOST";
    }
    if (cpu_temperature && policy_.maximum_cpu_temperature_celsius &&
        *cpu_temperature > *policy_.maximum_cpu_temperature_celsius) {
        return "CPU_TEMPERATURE_THRESHOLD";
    }
    const auto cpu_power = metric_value(snapshot.cpu_power_watts);
    if (policy_.require_cpu_power && !cpu_power) {
        return "CPU_POWER_SENSOR_LOST";
    }
    if (cpu_power && policy_.maximum_cpu_power_watts &&
        *cpu_power > *policy_.maximum_cpu_power_watts) {
        return "CPU_POWER_THRESHOLD";
    }
    const auto temperature = metric_value(snapshot.gpu_temperature_celsius);
    if (policy_.require_gpu_temperature && !temperature) {
        return "GPU_TEMPERATURE_SENSOR_LOST";
    }
    if (temperature && policy_.maximum_gpu_temperature_celsius &&
        *temperature > *policy_.maximum_gpu_temperature_celsius) {
        return "GPU_TEMPERATURE_THRESHOLD";
    }
    const auto power = metric_value(snapshot.gpu_power_watts);
    if (policy_.require_gpu_power && !power) {
        return "GPU_POWER_SENSOR_LOST";
    }
    if (power && policy_.maximum_gpu_power_watts && *power > *policy_.maximum_gpu_power_watts) {
        return "GPU_POWER_THRESHOLD";
    }
    const auto ram_available = metric_value(snapshot.ram_available_bytes);
    if (policy_.require_ram_available && !ram_available) {
        return "RAM_AVAILABLE_SENSOR_LOST";
    }
    if (ram_available && policy_.minimum_ram_available_bytes &&
        *ram_available < static_cast<double>(*policy_.minimum_ram_available_bytes)) {
        return "RAM_AVAILABLE_THRESHOLD";
    }
    const auto vram_free = metric_value(snapshot.vram_free_mib);
    if (policy_.require_vram_free && !vram_free) {
        return "VRAM_FREE_SENSOR_LOST";
    }
    if (vram_free && policy_.minimum_vram_free_mib && *vram_free < *policy_.minimum_vram_free_mib) {
        return "VRAM_FREE_THRESHOLD";
    }
    const auto whea_errors = metric_value(snapshot.whea_errors_recent);
    if (policy_.require_whea_status && !whea_errors) {
        return "WHEA_STATUS_LOST";
    }
    if (whea_errors && *whea_errors > 0.0) {
        return "WHEA_ERROR_DETECTED";
    }
    const auto xid_errors = metric_value(snapshot.nvidia_xid_errors_recent);
    if (policy_.require_nvidia_xid_status && !xid_errors) {
        return "NVIDIA_XID_STATUS_LOST";
    }
    if (xid_errors && *xid_errors > 0.0) {
        return "NVIDIA_XID_DETECTED";
    }
    return std::nullopt;
}

WatchdogOutcome BenchmarkWatchdog::tick(
    const std::uint64_t now_milliseconds,
    const HardwareSnapshot& snapshot,
    const bool external_stop_requested) {
    if (!worker_.alive()) {
        const auto exit_code = worker_.exit_code();
        if (exit_code && *exit_code != 0) {
            performance_valid_ = false;
            if (stop_reason_ == "NONE") {
                stop_reason_ = "WORKER_EXIT_NONZERO";
            }
        }
        logger_.append(
            "worker_exited",
            "{\"exit_code\":" + internal::json_escape(
                exit_code ? std::to_string(*exit_code) : std::string{"UNKNOWN"}) +
                ",\"reason\":" + internal::json_escape(stop_reason_) + "}",
            utc_now());
        return {WatchdogDecision::worker_exited, performance_valid_, stop_reason_};
    }

    logger_.append("telemetry", "{\"snapshot\":" + snapshot.canonical_json() + "}", snapshot.utc);
    const auto unsafe = unsafe_reason(snapshot);
    if (unsafe) {
        performance_valid_ = false;
    }
    if (!stop_requested_ && (unsafe || external_stop_requested)) {
        stop_reason_ = unsafe ? *unsafe : "EXTERNAL_GRACEFUL_STOP";
        stop_requested_ = true;
        stop_requested_at_ = now_milliseconds;
        worker_.request_graceful_stop();
        logger_.append(
            "graceful_stop_requested",
            "{\"performance_valid\":" + std::string(performance_valid_ ? "true" : "false") +
                ",\"reason\":" + internal::json_escape(stop_reason_) + "}",
            utc_now());
        return {WatchdogDecision::graceful_stop_requested, performance_valid_, stop_reason_};
    }
    if (stop_requested_) {
        if (now_milliseconds >= stop_requested_at_ &&
            now_milliseconds - stop_requested_at_ >= policy_.graceful_timeout_milliseconds) {
            worker_.force_stop();
            logger_.append(
                "forced_stop",
                "{\"reason\":" + internal::json_escape(stop_reason_) + "}",
                utc_now());
            return {WatchdogDecision::forced_stop, performance_valid_, stop_reason_};
        }
        return {WatchdogDecision::graceful_stop_requested, performance_valid_, stop_reason_};
    }
    return {WatchdogDecision::continue_monitoring, performance_valid_, "NONE"};
}

std::string to_string(const WatchdogDecision decision) {
    switch (decision) {
    case WatchdogDecision::continue_monitoring: return "CONTINUE";
    case WatchdogDecision::graceful_stop_requested: return "GRACEFUL_STOP_REQUESTED";
    case WatchdogDecision::forced_stop: return "FORCED_STOP";
    case WatchdogDecision::worker_exited: return "WORKER_EXITED";
    }
    return "UNKNOWN";
}

} // namespace primeforge::runtime
