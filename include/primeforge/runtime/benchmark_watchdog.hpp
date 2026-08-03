// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/runtime/benchmark_logger.hpp"
#include "primeforge/runtime/hardware_monitor.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace primeforge::runtime {

class WorkerController {
public:
    virtual ~WorkerController() = default;
    [[nodiscard]] virtual bool alive() const = 0;
    virtual void request_graceful_stop() = 0;
    virtual void force_stop() = 0;
};

class ProcessWorkerController final : public WorkerController {
public:
    ProcessWorkerController(std::uint64_t process_id, std::filesystem::path stop_file);
    [[nodiscard]] bool alive() const override;
    void request_graceful_stop() override;
    void force_stop() override;

private:
    std::uint64_t process_id_{};
    std::filesystem::path stop_file_;
};

struct WatchdogPolicy {
    std::uint64_t graceful_timeout_milliseconds{30'000U};
    std::optional<double> maximum_cpu_temperature_celsius;
    std::optional<double> maximum_cpu_power_watts;
    std::optional<double> maximum_gpu_temperature_celsius;
    std::optional<double> maximum_gpu_power_watts;
    bool require_cpu_temperature{};
    bool require_cpu_power{};
    bool require_gpu_temperature{};
    bool require_gpu_power{};
};

enum class WatchdogDecision {
    continue_monitoring,
    graceful_stop_requested,
    forced_stop,
    worker_exited
};

struct WatchdogOutcome {
    WatchdogDecision decision{WatchdogDecision::continue_monitoring};
    bool performance_valid{true};
    std::string reason{"NONE"};
};

class BenchmarkWatchdog {
public:
    BenchmarkWatchdog(
        WatchdogPolicy policy,
        WorkerController& worker,
        BenchmarkLogger& logger);

    [[nodiscard]] WatchdogOutcome tick(
        std::uint64_t now_milliseconds,
        const HardwareSnapshot& snapshot,
        bool external_stop_requested = false);

private:
    [[nodiscard]] std::optional<std::string> unsafe_reason(const HardwareSnapshot& snapshot) const;

    WatchdogPolicy policy_;
    WorkerController& worker_;
    BenchmarkLogger& logger_;
    bool stop_requested_{};
    bool performance_valid_{true};
    std::uint64_t stop_requested_at_{};
    std::string stop_reason_{"NONE"};
};

[[nodiscard]] std::string to_string(WatchdogDecision decision);

} // namespace primeforge::runtime
