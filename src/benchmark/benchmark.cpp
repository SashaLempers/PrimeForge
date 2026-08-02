// SPDX-License-Identifier: Apache-2.0

#include "primeforge/benchmark/benchmark.hpp"

#include <algorithm>
#include <stdexcept>

namespace primeforge::benchmark {
namespace {

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::uint64_t median_of_sorted(const std::vector<std::uint64_t>& sorted) {
    return sorted[(sorted.size() - 1U) / 2U];
}

}  // namespace

SummaryStatistics summarize(const std::span<const std::uint64_t> samples) {
    if (samples.size() < 7U) {
        throw std::invalid_argument("at least seven samples are required");
    }

    std::vector<std::uint64_t> sorted(samples.begin(), samples.end());
    std::ranges::sort(sorted);
    const auto median = median_of_sorted(sorted);

    std::vector<std::uint64_t> deviations;
    deviations.reserve(sorted.size());
    for (const auto value : sorted) {
        deviations.push_back(value >= median ? value - median : median - value);
    }
    std::ranges::sort(deviations);

    // [min,max] is a conservative distribution-free interval for the population
    // median at n>=7 (coverage >= 98.4375% under independent continuous samples).
    return {
        sorted.front(),
        sorted.back(),
        median,
        median_of_sorted(deviations),
        sorted.front(),
        sorted.back(),
    };
}

std::vector<std::size_t> randomized_variant_schedule(
    const std::size_t variant_count, const std::size_t repetitions, const std::uint64_t seed) {
    if (variant_count == 0U || repetitions == 0U) {
        throw std::invalid_argument("variant count and repetitions must be nonzero");
    }

    std::vector<std::size_t> result;
    result.reserve(variant_count * repetitions);
    std::uint64_t state = seed;
    for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
        std::vector<std::size_t> order(variant_count);
        for (std::size_t index = 0; index < variant_count; ++index) {
            order[index] = index;
        }
        for (std::size_t index = variant_count; index > 1U; --index) {
            const auto selected = static_cast<std::size_t>(splitmix64(state) % index);
            std::swap(order[index - 1U], order[selected]);
        }
        result.insert(result.end(), order.begin(), order.end());
    }
    return result;
}

bool confidence_intervals_overlap(
    const SummaryStatistics& left, const SummaryStatistics& right) noexcept {
    return left.confidence_low <= right.confidence_high &&
           right.confidence_low <= left.confidence_high;
}

TelemetryAssessment assess_telemetry(
    const TelemetrySample& sample, const TelemetryPolicy& policy) noexcept {
    if (sample.hardware_error) {
        return TelemetryAssessment::invalid_hardware_error;
    }
    if (sample.throttle_reported) {
        return TelemetryAssessment::invalid_throttling;
    }
    if ((policy.maximum_cpu_temperature_millicelsius.has_value() &&
         sample.cpu_temperature_millicelsius.has_value() &&
         *sample.cpu_temperature_millicelsius > *policy.maximum_cpu_temperature_millicelsius) ||
        (policy.maximum_gpu_temperature_millicelsius.has_value() &&
         sample.gpu_temperature_millicelsius.has_value() &&
         *sample.gpu_temperature_millicelsius > *policy.maximum_gpu_temperature_millicelsius)) {
        return TelemetryAssessment::invalid_temperature;
    }
    if ((policy.minimum_cpu_frequency_hz.has_value() && sample.cpu_frequency_hz.has_value() &&
         *sample.cpu_frequency_hz < *policy.minimum_cpu_frequency_hz) ||
        (policy.minimum_gpu_frequency_hz.has_value() && sample.gpu_frequency_hz.has_value() &&
         *sample.gpu_frequency_hz < *policy.minimum_gpu_frequency_hz)) {
        return TelemetryAssessment::invalid_frequency;
    }
    if (!sample.cpu_temperature_millicelsius.has_value() &&
        !sample.cpu_frequency_hz.has_value() &&
        !sample.gpu_temperature_millicelsius.has_value() &&
        !sample.gpu_frequency_hz.has_value() &&
        !sample.power_milliwatts.has_value()) {
        return TelemetryAssessment::unavailable;
    }
    return TelemetryAssessment::valid;
}

std::string to_string(const TelemetryAssessment assessment) {
    switch (assessment) {
        case TelemetryAssessment::valid:
            return "VALID";
        case TelemetryAssessment::unavailable:
            return "UNAVAILABLE";
        case TelemetryAssessment::invalid_hardware_error:
            return "INVALID_HARDWARE_ERROR";
        case TelemetryAssessment::invalid_throttling:
            return "INVALID_THROTTLING";
        case TelemetryAssessment::invalid_temperature:
            return "INVALID_TEMPERATURE";
        case TelemetryAssessment::invalid_frequency:
            return "INVALID_FREQUENCY";
    }
    return "UNKNOWN";
}

}  // namespace primeforge::benchmark
