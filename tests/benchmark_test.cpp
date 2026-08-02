// SPDX-License-Identifier: Apache-2.0

#include "primeforge/benchmark/benchmark.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_statistics() {
    constexpr std::array<std::uint64_t, 7> samples{7, 1, 5, 3, 2, 6, 4};
    const auto statistics = primeforge::benchmark::summarize(samples);
    check(statistics.minimum == 1, "minimum");
    check(statistics.maximum == 7, "maximum");
    check(statistics.median == 4, "median");
    check(statistics.median_absolute_deviation == 2, "median absolute deviation");
    check(statistics.confidence_low == 1 && statistics.confidence_high == 7, "confidence interval");

    bool rejected = false;
    try {
        constexpr std::array<std::uint64_t, 6> too_short{1, 2, 3, 4, 5, 6};
        static_cast<void>(primeforge::benchmark::summarize(too_short));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "fewer than seven samples rejected");
}

void test_schedule() {
    const auto first = primeforge::benchmark::randomized_variant_schedule(3, 9, 20260802);
    const auto second = primeforge::benchmark::randomized_variant_schedule(3, 9, 20260802);
    check(first == second, "schedule deterministic for a fixed seed");
    check(first.size() == 27, "schedule size");
    for (std::size_t repetition = 0; repetition < 9; ++repetition) {
        std::array<bool, 3> seen{};
        for (std::size_t offset = 0; offset < 3; ++offset) {
            seen[first[repetition * 3 + offset]] = true;
        }
        check(seen[0] && seen[1] && seen[2], "each repetition contains every variant exactly once");
    }
}

void test_compatibility() {
    const primeforge::benchmark::SummaryStatistics left{10, 20, 15, 2, 10, 20};
    const primeforge::benchmark::SummaryStatistics compatible{18, 30, 23, 3, 18, 30};
    const primeforge::benchmark::SummaryStatistics incompatible{21, 30, 25, 2, 21, 30};
    check(primeforge::benchmark::confidence_intervals_overlap(left, compatible), "overlap accepted");
    check(!primeforge::benchmark::confidence_intervals_overlap(left, incompatible), "separation rejected");
}

void test_telemetry() {
    using primeforge::benchmark::TelemetryAssessment;
    using primeforge::benchmark::TelemetryPolicy;
    using primeforge::benchmark::TelemetrySample;

    const TelemetryPolicy policy{90'000, 3'000'000'000ULL, 85'000, 1'500'000'000ULL};
    check(primeforge::benchmark::assess_telemetry({}, policy) == TelemetryAssessment::unavailable,
          "unavailable telemetry remains explicit");

    TelemetrySample valid{70'000, 4'000'000'000ULL, 60'000, 2'000'000'000ULL, 250'000, false, false};
    check(primeforge::benchmark::assess_telemetry(valid, policy) == TelemetryAssessment::valid, "valid telemetry");
    valid.throttle_reported = true;
    check(primeforge::benchmark::assess_telemetry(valid, policy) == TelemetryAssessment::invalid_throttling,
          "reported throttling invalidates a sample");
    valid.throttle_reported = false;
    valid.cpu_temperature_millicelsius = 95'000;
    check(primeforge::benchmark::assess_telemetry(valid, policy) == TelemetryAssessment::invalid_temperature,
          "temperature threshold invalidates a sample");
    valid.cpu_temperature_millicelsius = 70'000;
    valid.cpu_frequency_hz = 2'000'000'000ULL;
    check(primeforge::benchmark::assess_telemetry(valid, policy) == TelemetryAssessment::invalid_frequency,
          "frequency collapse invalidates a sample");
    valid.hardware_error = true;
    check(primeforge::benchmark::assess_telemetry(valid, policy) == TelemetryAssessment::invalid_hardware_error,
          "hardware error has highest priority");
}

}  // namespace

int main() {
    try {
        test_statistics();
        test_schedule();
        test_compatibility();
        test_telemetry();
        std::cout << "primeforge-benchmark-tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-benchmark-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
