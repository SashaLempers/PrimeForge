// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            throw std::invalid_argument("usage: watchdog-fixture graceful|hang PID_FILE STOP_FILE");
        }
        const std::string mode = argv[1];
        const std::filesystem::path pid_file = argv[2];
        const std::filesystem::path stop_file = argv[3];
#ifdef _WIN32
        const auto process_id = static_cast<unsigned long long>(GetCurrentProcessId());
#else
        const auto process_id = static_cast<unsigned long long>(getpid());
#endif
        {
            std::ofstream output(pid_file, std::ios::binary | std::ios::trunc);
            output << process_id << '\n' << std::flush;
            if (!output) { throw std::runtime_error("cannot publish fixture pid"); }
        }
        while (true) {
            if (mode == "graceful" && std::filesystem::exists(stop_file)) {
                return 0;
            }
            if (mode != "graceful" && mode != "hang") {
                throw std::invalid_argument("invalid fixture mode");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (const std::exception& error) {
        std::cerr << "watchdog-fixture: " << error.what() << '\n';
        return 1;
    }
}
