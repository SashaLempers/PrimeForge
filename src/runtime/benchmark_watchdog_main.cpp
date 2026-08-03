// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/benchmark_watchdog.hpp"

#include "primeforge/core/sha256.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
std::atomic_bool signal_stop{false};
void on_signal(int) { signal_stop.store(true); }
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
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--pid" && index + 1 < argc) { pid = std::stoull(argv[++index]); }
            else if (argument == "--stop-file" && index + 1 < argc) { stop_file = argv[++index]; }
            else if (argument == "--watchdog-stop-file" && index + 1 < argc) { watchdog_stop_file = argv[++index]; }
            else if (argument == "--log" && index + 1 < argc) { log_path = argv[++index]; }
            else if (argument == "--campaign" && index + 1 < argc) { campaign = argv[++index]; }
            else if (argument == "--checkpoint" && index + 1 < argc) { checkpoint_path = argv[++index]; }
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
            else { throw std::invalid_argument("invalid benchmark_watchdog argument"); }
        }
        if (pid == 0U || interval_ms == 0U || stop_file.empty() || log_path.empty() || campaign.empty()) {
            throw std::invalid_argument("--pid, --stop-file, --log, --campaign and positive --interval-ms are required");
        }

        primeforge::runtime::BenchmarkLogger logger(log_path, campaign);
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
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        while (true) {
            const auto snapshot = monitor.sample();
            const bool external_stop = signal_stop.load() ||
                (!watchdog_stop_file.empty() && std::filesystem::exists(watchdog_stop_file));
            const auto outcome = watchdog.tick(snapshot.monotonic_milliseconds, snapshot, external_stop);
            std::cout << "benchmark_watchdog.decision=" << primeforge::runtime::to_string(outcome.decision)
                      << " reason=" << outcome.reason << '\n' << std::flush;
            if (outcome.decision == primeforge::runtime::WatchdogDecision::forced_stop ||
                outcome.decision == primeforge::runtime::WatchdogDecision::worker_exited) {
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    } catch (const std::exception& error) {
        std::cerr << "benchmark_watchdog: " << error.what() << '\n';
        return 1;
    }
}
