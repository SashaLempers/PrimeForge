// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/hardware_monitor.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
std::atomic_bool stop_requested{false};
void on_signal(int) { stop_requested.store(true); }
}

int main(int argc, char** argv) {
    try {
        std::uint64_t samples = 1U;
        std::uint64_t interval_ms = 1'000U;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--samples" && index + 1 < argc) {
                samples = std::stoull(argv[++index]);
            } else if (argument == "--interval-ms" && index + 1 < argc) {
                interval_ms = std::stoull(argv[++index]);
            } else if (argument == "--once") {
                samples = 1U;
            } else {
                throw std::invalid_argument("usage: hardware_monitor [--once] [--samples N] [--interval-ms N]");
            }
        }
        if (interval_ms == 0U) { throw std::invalid_argument("interval must be positive"); }
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        primeforge::runtime::HardwareMonitor monitor;
        for (std::uint64_t index = 0U; !stop_requested.load() && (samples == 0U || index < samples); ++index) {
            std::cout << monitor.sample().canonical_json() << '\n' << std::flush;
            if (samples == 0U || index + 1U < samples) {
                std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "hardware_monitor: " << error.what() << '\n';
        return 1;
    }
}
