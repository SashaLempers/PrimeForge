// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/benchmark_logger.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        std::string path;
        std::string campaign;
        std::string event;
        std::string payload{"{}"};
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--log" && index + 1 < argc) { path = argv[++index]; }
            else if (argument == "--campaign" && index + 1 < argc) { campaign = argv[++index]; }
            else if (argument == "--event" && index + 1 < argc) { event = argv[++index]; }
            else if (argument == "--payload-json" && index + 1 < argc) { payload = argv[++index]; }
            else { throw std::invalid_argument("usage: benchmark_logger --log PATH --campaign ID --event TYPE [--payload-json OBJECT]"); }
        }
        if (path.empty() || campaign.empty() || event.empty()) {
            throw std::invalid_argument("log path, campaign and event are required");
        }
        primeforge::runtime::BenchmarkLogger logger(path, campaign);
        logger.append(event, payload, primeforge::runtime::utc_now());
        std::cout << "benchmark_logger.status=PASS\n";
        std::cout << "benchmark_logger.next_sequence=" << logger.next_sequence() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "benchmark_logger: " << error.what() << '\n';
        return 1;
    }
}
