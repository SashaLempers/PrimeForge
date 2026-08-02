// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace primeforge::benchmark {

struct SummaryStatistics {
    std::uint64_t minimum{};
    std::uint64_t maximum{};
    std::uint64_t median{};
    std::uint64_t median_absolute_deviation{};
    std::uint64_t confidence_low{};
    std::uint64_t confidence_high{};
};

struct TelemetrySample {
    std::optional<std::int64_t> cpu_temperature_millicelsius;
    std::optional<std::uint64_t> cpu_frequency_hz;
    std::optional<std::int64_t> gpu_temperature_millicelsius;
    std::optional<std::uint64_t> gpu_frequency_hz;
    std::optional<std::uint64_t> power_milliwatts;
    bool hardware_error{};
    bool throttle_reported{};
};

struct TelemetryPolicy {
    std::optional<std::int64_t> maximum_cpu_temperature_millicelsius;
    std::optional<std::uint64_t> minimum_cpu_frequency_hz;
    std::optional<std::int64_t> maximum_gpu_temperature_millicelsius;
    std::optional<std::uint64_t> minimum_gpu_frequency_hz;
};

enum class TelemetryAssessment {
    valid,
    unavailable,
    invalid_hardware_error,
    invalid_throttling,
    invalid_temperature,
    invalid_frequency
};

[[nodiscard]] SummaryStatistics summarize(std::span<const std::uint64_t> samples);
[[nodiscard]] std::vector<std::size_t> randomized_variant_schedule(
    std::size_t variant_count, std::size_t repetitions, std::uint64_t seed);
[[nodiscard]] bool confidence_intervals_overlap(
    const SummaryStatistics& left, const SummaryStatistics& right) noexcept;
[[nodiscard]] TelemetryAssessment assess_telemetry(
    const TelemetrySample& sample, const TelemetryPolicy& policy) noexcept;
[[nodiscard]] std::string to_string(TelemetryAssessment assessment);

}  // namespace primeforge::benchmark
