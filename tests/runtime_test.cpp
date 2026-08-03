// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/runtime/benchmark_logger.hpp"
#include "primeforge/runtime/benchmark_watchdog.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"
#include "primeforge/runtime/hardware_monitor.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

template <typename Function>
void expect_throw(Function&& function, const std::string& message) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    check(threw, message);
}

class MockWorker final : public primeforge::runtime::WorkerController {
public:
    bool running{true};
    unsigned graceful_requests{};
    unsigned forced_stops{};
    std::optional<int> exit_code_value{0};

    [[nodiscard]] bool alive() const override { return running; }
    [[nodiscard]] std::optional<int> exit_code() const override { return exit_code_value; }
    void request_graceful_stop() override { ++graceful_requests; }
    void force_stop() override { ++forced_stops; running = false; }
};

[[nodiscard]] primeforge::runtime::HardwareSnapshot safe_snapshot() {
    primeforge::runtime::HardwareSnapshot snapshot;
    snapshot.utc = "2026-08-02T00:00:00.000Z";
    snapshot.cpu_temperature_celsius = {"fake.cpu.temperature", "60"};
    snapshot.cpu_power_watts = {"fake.cpu.power", "100"};
    snapshot.ram_available_bytes = {"fake.ram.available", "42949672960"};
    snapshot.gpu_temperature_celsius = {"fake.temperature", "60"};
    snapshot.gpu_power_watts = {"fake.power", "100"};
    snapshot.vram_free_mib = {"fake.vram.free", "12000"};
    snapshot.whea_errors_recent = {"fake.whea", "0"};
    snapshot.throttling_reasons = "NONE";
    return snapshot;
}

void test_hardware_monitor() {
    bool nvidia_invoked = false;
    bool whea_invoked = false;
    const auto lconnect = std::string{"{\"LastTime\":\""} + primeforge::runtime::utc_now() +
        "\",\"CPUTemperature\":63.25,\"CPUPower\":107.5,\"CPUClockRate\":5550}";
    primeforge::runtime::HardwareMonitor monitor(
        [&nvidia_invoked, &whea_invoked](const std::string_view command) -> std::optional<std::string> {
            if (command.find("wevtutil") != std::string_view::npos) {
                whea_invoked = true;
                return "";
            }
            nvidia_invoked = command.find("clocks_event_reasons") != std::string_view::npos;
            return "71, N/A, 250.50, 360.00, 2700, 15001, 99, 20, 4096, 12207, Active, Not Active, Not Active, Not Active\n";
        },
        [&lconnect]() -> std::optional<std::string> { return lconnect; });
    const auto snapshot = monitor.sample();
    check(nvidia_invoked, "hardware monitor uses the fixed NVIDIA query");
#ifdef _WIN32
    check(whea_invoked && snapshot.whea_errors_recent.value == "0",
          "Windows hardware monitor checks recent WHEA events");
#else
    check(!whea_invoked && !snapshot.whea_errors_recent.available(),
          "non-Windows hardware monitor keeps WHEA unavailable");
#endif
    check(snapshot.gpu_temperature_celsius.value == "71", "GPU temperature parsed");
    check(snapshot.gpu_memory_temperature_celsius.value == "UNKNOWN", "N/A remains UNKNOWN");
    check(snapshot.gpu_sm_clock_mhz.value == "2700", "GPU frequency parsed");
    check(snapshot.gpu_power_watts.value == "250.50", "GPU power parsed");
    check(snapshot.cpu_temperature_celsius.value == "63.25", "L-Connect CPU temperature parsed");
    check(snapshot.cpu_power_watts.value == "107.5", "L-Connect CPU power parsed");
    check(snapshot.cpu_frequency_mhz.value == "5550", "L-Connect CPU frequency parsed");
    check(snapshot.throttling_detected, "active NVIDIA clock-event reason is throttling");
    check(snapshot.throttling_reasons == "SW_POWER_CAP", "exact throttle reason retained");
    check(snapshot.canonical_json().find("\"cpu_power_watts\"") != std::string::npos,
          "snapshot serializes UNKNOWN fields");

    primeforge::runtime::HardwareMonitor unavailable(
        [](std::string_view) -> std::optional<std::string> { return std::nullopt; },
        []() -> std::optional<std::string> { return std::nullopt; });
    const auto absent = unavailable.sample();
    check(!absent.gpu_temperature_celsius.available(), "missing NVIDIA provider remains UNKNOWN");
    check(absent.throttling_reasons == "UNKNOWN", "missing throttle provider remains UNKNOWN");

    const auto invalid_lconnect = std::string{"{\"LastTime\":\""} + primeforge::runtime::utc_now() +
        "\",\"CPUTemperature\":0,\"CPUPower\":0,\"CPUClockRate\":0}";
    primeforge::runtime::HardwareMonitor invalid(
        [](std::string_view) -> std::optional<std::string> { return std::nullopt; },
        [&invalid_lconnect]() -> std::optional<std::string> { return invalid_lconnect; });
    const auto rejected = invalid.sample();
    check(!rejected.cpu_temperature_celsius.available(), "zero CPU temperature is rejected");
    check(!rejected.cpu_power_watts.available(), "zero CPU power is rejected");

    primeforge::runtime::HardwareMonitor stale(
        [](std::string_view) -> std::optional<std::string> { return std::nullopt; },
        []() -> std::optional<std::string> {
            return "{\"LastTime\":\"2020-01-01T00:00:00.000Z\",\"CPUTemperature\":70,\"CPUPower\":120}";
        });
    check(!stale.sample().cpu_temperature_celsius.available(), "stale local telemetry is rejected");
}

void test_logger(const std::filesystem::path& directory) {
    const auto path = directory / "events.jsonl";
    {
        primeforge::runtime::BenchmarkLogger logger(path, "campaign-test");
        logger.append("start", "{\"state\":\"RUNNING\"}", "2026-08-02T00:00:00.000Z");
        logger.append("sample", "{\"value\":\"1\"}", "2026-08-02T00:00:01.000Z");
        check(logger.next_sequence() == 3U, "logger sequence advances");
    }
    {
        primeforge::runtime::BenchmarkLogger resumed(path, "campaign-test");
        check(resumed.next_sequence() == 3U, "logger resumes after reopen");
        resumed.append("resume", "{}", "2026-08-02T00:00:02.000Z");
    }
    expect_throw(
        [&] { primeforge::runtime::BenchmarkLogger wrong(path, "another-campaign"); },
        "logger rejects a different campaign id");
    {
        std::ofstream truncated(path, std::ios::binary | std::ios::app);
        truncated << "{partial";
    }
    expect_throw(
        [&] { primeforge::runtime::BenchmarkLogger broken(path, "campaign-test"); },
        "logger rejects an incomplete final record");
}

void test_checkpoints(const std::filesystem::path& directory) {
    primeforge::PortableSha256Provider sha256;
    primeforge::runtime::CheckpointManager manager(sha256);
    const auto path = directory / "campaign.checkpoint.json";
    const primeforge::runtime::CheckpointState expected{
        "campaign-checkpoint", "batch=17;note=escaped\\value", "184467440737095516160", 42U};
    manager.save(path, expected);
    check(manager.valid(path), "saved checkpoint validates");
    check(manager.load(path) == expected, "checkpoint resumes exact opaque state");
    {
        std::fstream corrupt(path, std::ios::binary | std::ios::in | std::ios::out);
        check(static_cast<bool>(corrupt), "checkpoint opened for mutation");
        corrupt.seekp(20);
        corrupt.put('X');
    }
    check(!manager.valid(path), "corrupt checkpoint is rejected");
    expect_throw([&] { static_cast<void>(manager.load(path)); }, "corrupt checkpoint load fails");
}

void test_watchdog(const std::filesystem::path& directory) {
    {
        MockWorker worker;
        worker.running = false;
        worker.exit_code_value = 7;
        primeforge::runtime::BenchmarkLogger logger(directory / "worker-exit.jsonl", "worker-exit");
        primeforge::runtime::WatchdogPolicy policy;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        const auto exited = watchdog.tick(1U, safe_snapshot());
        check(exited.decision == primeforge::runtime::WatchdogDecision::worker_exited &&
                  exited.reason == "WORKER_EXIT_NONZERO" && !exited.performance_valid,
              "nonzero worker exit invalidates the campaign");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "memory-threshold.jsonl", "memory-threshold");
        primeforge::runtime::WatchdogPolicy policy;
        policy.minimum_ram_available_bytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
        policy.minimum_vram_free_mib = 4096.0;
        policy.require_ram_available = true;
        policy.require_vram_free = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.ram_available_bytes.value = "1073741824";
        const auto memory = watchdog.tick(1U, snapshot);
        check(memory.reason == "RAM_AVAILABLE_THRESHOLD", "low available RAM stops safely");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "memory-lost.jsonl", "memory-lost");
        primeforge::runtime::WatchdogPolicy policy;
        policy.require_ram_available = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.ram_available_bytes.value = "UNKNOWN";
        const auto memory = watchdog.tick(1U, snapshot);
        check(memory.reason == "RAM_AVAILABLE_SENSOR_LOST", "lost available-RAM provider stops safely");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "vram-threshold.jsonl", "vram-threshold");
        primeforge::runtime::WatchdogPolicy policy;
        policy.minimum_vram_free_mib = 4096.0;
        policy.require_vram_free = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.vram_free_mib.value = "2048";
        const auto memory = watchdog.tick(1U, snapshot);
        check(memory.reason == "VRAM_FREE_THRESHOLD", "low free VRAM stops safely");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "vram-lost.jsonl", "vram-lost");
        primeforge::runtime::WatchdogPolicy policy;
        policy.require_vram_free = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.vram_free_mib.value = "UNKNOWN";
        const auto memory = watchdog.tick(1U, snapshot);
        check(memory.reason == "VRAM_FREE_SENSOR_LOST", "lost free-VRAM provider stops safely");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "whea-error.jsonl", "whea-error");
        primeforge::runtime::WatchdogPolicy policy;
        policy.require_whea_status = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.whea_errors_recent.value = "1";
        const auto hardware = watchdog.tick(1U, snapshot);
        check(hardware.reason == "WHEA_ERROR_DETECTED", "recent WHEA event stops safely");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "whea-lost.jsonl", "whea-lost");
        primeforge::runtime::WatchdogPolicy policy;
        policy.require_whea_status = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.whea_errors_recent.value = "UNKNOWN";
        const auto hardware = watchdog.tick(1U, snapshot);
        check(hardware.reason == "WHEA_STATUS_LOST", "lost WHEA provider stops safely");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "invalid-policy.jsonl", "invalid-policy");
        primeforge::runtime::WatchdogPolicy policy;
        policy.maximum_cpu_temperature_celsius = std::nan("");
        bool rejected = false;
        try {
            primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "nonfinite watchdog threshold is rejected");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "cpu-threshold.jsonl", "cpu-threshold");
        primeforge::runtime::WatchdogPolicy policy;
        policy.maximum_cpu_temperature_celsius = 92.0;
        policy.require_cpu_temperature = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.cpu_temperature_celsius.value = "92.1";
        const auto result = watchdog.tick(1U, snapshot);
        check(result.reason == "CPU_TEMPERATURE_THRESHOLD" && worker.graceful_requests == 1U,
              "CPU threshold requests a graceful stop");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "cpu-sensor.jsonl", "cpu-sensor");
        primeforge::runtime::WatchdogPolicy policy;
        policy.require_cpu_temperature = true;
        policy.require_cpu_power = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.cpu_temperature_celsius.value = "UNKNOWN";
        const auto temperature = watchdog.tick(1U, snapshot);
        check(temperature.reason == "CPU_TEMPERATURE_SENSOR_LOST",
              "required CPU temperature loss stops safely");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "cpu-power-sensor.jsonl", "cpu-power-sensor");
        primeforge::runtime::WatchdogPolicy policy;
        policy.require_cpu_power = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.cpu_power_watts.value = "UNKNOWN";
        const auto power = watchdog.tick(1U, snapshot);
        check(power.reason == "CPU_POWER_SENSOR_LOST",
              "required CPU power loss stops safely");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "cpu-nonfinite.jsonl", "cpu-nonfinite");
        primeforge::runtime::WatchdogPolicy policy;
        policy.require_cpu_temperature = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.cpu_temperature_celsius.value = "nan";
        const auto temperature = watchdog.tick(1U, snapshot);
        check(temperature.reason == "CPU_TEMPERATURE_SENSOR_LOST",
              "nonfinite CPU telemetry is treated as sensor loss");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "cpu-power-threshold.jsonl", "cpu-power-threshold");
        primeforge::runtime::WatchdogPolicy policy;
        policy.maximum_cpu_power_watts = 200.0;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.cpu_power_watts.value = "200.1";
        const auto power = watchdog.tick(1U, snapshot);
        check(power.reason == "CPU_POWER_THRESHOLD",
              "CPU power threshold requests a graceful stop");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "threshold.jsonl", "threshold");
        primeforge::runtime::WatchdogPolicy policy;
        policy.graceful_timeout_milliseconds = 100U;
        policy.maximum_gpu_temperature_celsius = 80.0;
        policy.require_gpu_temperature = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.gpu_temperature_celsius.value = "81";
        const auto requested = watchdog.tick(10U, snapshot);
        check(requested.decision == primeforge::runtime::WatchdogDecision::graceful_stop_requested,
              "threshold requests graceful stop");
        check(!requested.performance_valid && worker.graceful_requests == 1U,
              "threshold invalidates performance and contacts worker once");
        const auto forced = watchdog.tick(10U + 7U * 24U * 60U * 60U * 1000U, snapshot);
        check(forced.decision == primeforge::runtime::WatchdogDecision::forced_stop,
              "multi-day monotonic interval forces a hung worker without overflow");
        check(worker.forced_stops == 1U, "hung worker force-stop invoked");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "sensor.jsonl", "sensor");
        primeforge::runtime::WatchdogPolicy policy;
        policy.require_gpu_temperature = true;
        primeforge::runtime::BenchmarkWatchdog watchdog(policy, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.gpu_temperature_celsius.value = "UNKNOWN";
        const auto result = watchdog.tick(1U, snapshot);
        check(result.reason == "GPU_TEMPERATURE_SENSOR_LOST", "required sensor loss stops safely");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "throttle.jsonl", "throttle");
        primeforge::runtime::BenchmarkWatchdog watchdog({}, worker, logger);
        auto snapshot = safe_snapshot();
        snapshot.throttling_detected = true;
        snapshot.throttling_reasons = "HW_THERMAL_SLOWDOWN";
        const auto result = watchdog.tick(1U, snapshot);
        check(result.reason == "GPU_THROTTLING:HW_THERMAL_SLOWDOWN" && !result.performance_valid,
              "reported throttling invalidates and stops");
    }
    {
        MockWorker worker;
        primeforge::runtime::BenchmarkLogger logger(directory / "external.jsonl", "external");
        primeforge::runtime::BenchmarkWatchdog watchdog({}, worker, logger);
        const auto result = watchdog.tick(1U, safe_snapshot(), true);
        check(result.reason == "EXTERNAL_GRACEFUL_STOP" && result.performance_valid,
              "operator stop is graceful without falsifying valid prior samples");
        worker.running = false;
        const auto exited = watchdog.tick(2U, safe_snapshot());
        check(exited.decision == primeforge::runtime::WatchdogDecision::worker_exited,
              "clean worker exit is observed");
    }
}

} // namespace

int main() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() /
                           ("primeforge-runtime-tests-" + unique);
    try {
        std::filesystem::create_directories(directory);
        test_hardware_monitor();
        test_logger(directory);
        test_checkpoints(directory);
        test_watchdog(directory);
        std::filesystem::remove_all(directory);
        std::cout << "primeforge-runtime-tests: PASS\n";
        std::cout << "covered=local/GPU/WHEA monitor availability/loss/staleness, CPU/GPU temperature/frequency/power, RAM/VRAM thresholds, throttling, "
                     "durable logs, log resume/truncation, checkpoint resume/corruption, graceful/forced stop, "
                     "worker exit status, multi-day monotonic time\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << "primeforge-runtime-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
