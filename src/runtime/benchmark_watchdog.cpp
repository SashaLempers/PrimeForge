// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/benchmark_watchdog.hpp"

#include "primeforge/work/work_unit.hpp"
#include "runtime_internal.hpp"

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
    return internal::parse_double(metric.value);
}

} // namespace

ProcessWorkerController::ProcessWorkerController(
    const std::uint64_t process_id,
    std::filesystem::path stop_file)
    : process_id_(process_id), stop_file_(std::move(stop_file)) {
    if (process_id_ == 0U || !stop_file_.has_filename()) {
        throw std::invalid_argument("worker pid and stop-file path are required");
    }
}

bool ProcessWorkerController::alive() const {
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 static_cast<DWORD>(process_id_));
    if (process == nullptr) {
        return false;
    }
    const bool result = WaitForSingleObject(process, 0U) == WAIT_TIMEOUT;
    CloseHandle(process);
    return result;
#else
    const int result = kill(static_cast<pid_t>(process_id_), 0);
    return result == 0 || errno == EPERM;
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
    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(process_id_));
    if (process == nullptr) {
        if (alive()) {
            throw std::runtime_error("cannot open worker for forced termination");
        }
        return;
    }
    if (TerminateProcess(process, 3U) == 0) {
        CloseHandle(process);
        throw std::runtime_error("cannot force-stop worker");
    }
    static_cast<void>(WaitForSingleObject(process, 5'000U));
    CloseHandle(process);
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
}

std::optional<std::string> BenchmarkWatchdog::unsafe_reason(
    const HardwareSnapshot& snapshot) const {
    if (snapshot.throttling_detected) {
        return "GPU_THROTTLING:" + snapshot.throttling_reasons;
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
    return std::nullopt;
}

WatchdogOutcome BenchmarkWatchdog::tick(
    const std::uint64_t now_milliseconds,
    const HardwareSnapshot& snapshot,
    const bool external_stop_requested) {
    if (!worker_.alive()) {
        logger_.append("worker_exited", "{\"reason\":" + internal::json_escape(stop_reason_) + "}", utc_now());
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
