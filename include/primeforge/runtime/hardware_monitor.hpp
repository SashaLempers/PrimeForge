// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace primeforge::runtime {

struct Metric {
    std::string source;
    std::string value{"UNKNOWN"};

    [[nodiscard]] bool available() const noexcept { return value != "UNKNOWN"; }
};

struct HardwareSnapshot {
    std::string utc;
    std::uint64_t monotonic_milliseconds{};
    Metric cpu_frequency_mhz;
    Metric cpu_temperature_celsius;
    Metric cpu_power_watts;
    Metric ram_used_bytes;
    Metric ram_available_bytes;
    Metric gpu_name;
    Metric gpu_uuid;
    Metric gpu_temperature_celsius;
    Metric gpu_memory_temperature_celsius;
    Metric gpu_power_watts;
    Metric gpu_power_limit_watts;
    Metric gpu_sm_clock_mhz;
    Metric gpu_memory_clock_mhz;
    Metric gpu_utilization_percent;
    Metric gpu_memory_utilization_percent;
    Metric vram_used_mib;
    Metric vram_free_mib;
    Metric vram_total_mib;
    Metric whea_errors_recent;
    Metric nvidia_xid_errors_recent;
    bool throttling_detected{};
    std::string throttling_reasons{"UNKNOWN"};

    [[nodiscard]] std::string canonical_json() const;
};

using CommandRunner = std::function<std::optional<std::string>(std::string_view)>;
using LocalTelemetryReader = std::function<std::optional<std::string>()>;

class HardwareMonitor {
public:
    explicit HardwareMonitor(
        CommandRunner runner = {},
        LocalTelemetryReader local_telemetry_reader = {});
    [[nodiscard]] HardwareSnapshot sample() const;

private:
    CommandRunner runner_;
    LocalTelemetryReader local_telemetry_reader_;
};

} // namespace primeforge::runtime
