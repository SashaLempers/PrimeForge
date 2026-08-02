// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/runtime/benchmark_logger.hpp"
#include "primeforge/runtime/benchmark_watchdog.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"
#include "primeforge/runtime/hardware_monitor.hpp"

#include <chrono>
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

    [[nodiscard]] bool alive() const override { return running; }
    void request_graceful_stop() override { ++graceful_requests; }
    void force_stop() override { ++forced_stops; running = false; }
};

[[nodiscard]] primeforge::runtime::HardwareSnapshot safe_snapshot() {
    primeforge::runtime::HardwareSnapshot snapshot;
    snapshot.utc = "2026-08-02T00:00:00.000Z";
    snapshot.gpu_temperature_celsius = {"fake.temperature", "60"};
    snapshot.gpu_power_watts = {"fake.power", "100"};
    snapshot.throttling_reasons = "NONE";
    return snapshot;
}

void test_hardware_monitor() {
    bool invoked = false;
    primeforge::runtime::HardwareMonitor monitor(
        [&invoked](const std::string_view command) -> std::optional<std::string> {
            invoked = command.find("clocks_event_reasons") != std::string_view::npos;
            return "71, N/A, 250.50, 360.00, 2700, 15001, 99, 20, 4096, 12207, Active, Not Active, Not Active, Not Active\n";
        });
    const auto snapshot = monitor.sample();
    check(invoked, "hardware monitor uses the fixed NVIDIA query");
    check(snapshot.gpu_temperature_celsius.value == "71", "GPU temperature parsed");
    check(snapshot.gpu_memory_temperature_celsius.value == "UNKNOWN", "N/A remains UNKNOWN");
    check(snapshot.gpu_sm_clock_mhz.value == "2700", "GPU frequency parsed");
    check(snapshot.gpu_power_watts.value == "250.50", "GPU power parsed");
    check(snapshot.throttling_detected, "active NVIDIA clock-event reason is throttling");
    check(snapshot.throttling_reasons == "SW_POWER_CAP", "exact throttle reason retained");
    check(snapshot.canonical_json().find("\"cpu_power_watts\"") != std::string::npos,
          "snapshot serializes UNKNOWN fields");

    primeforge::runtime::HardwareMonitor unavailable(
        [](std::string_view) -> std::optional<std::string> { return std::nullopt; });
    const auto absent = unavailable.sample();
    check(!absent.gpu_temperature_celsius.available(), "missing NVIDIA provider remains UNKNOWN");
    check(absent.throttling_reasons == "UNKNOWN", "missing throttle provider remains UNKNOWN");
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
        std::cout << "covered=monitor availability/loss, temperature/frequency/power, throttling, "
                     "durable logs, log resume/truncation, checkpoint resume/corruption, graceful/forced stop, "
                     "multi-day monotonic time\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << "primeforge-runtime-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
