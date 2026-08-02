// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

[[nodiscard]] std::string quoted(const std::filesystem::path& path) {
    const std::string value = path.string();
    if (value.find_first_of(" \t\"'") != std::string::npos) {
        throw std::invalid_argument("watchdog process-test paths must not contain shell metacharacters");
    }
    return value;
}

[[nodiscard]] std::uint64_t await_pid(const std::filesystem::path& path) {
    for (unsigned attempt = 0U; attempt < 500U; ++attempt) {
        std::ifstream input(path);
        std::uint64_t pid{};
        if (input >> pid && pid != 0U) { return pid; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("fixture did not publish its pid");
}

void exercise(
    const std::filesystem::path& watchdog,
    const std::filesystem::path& fixture,
    const std::filesystem::path& directory,
    const std::string& mode) {
    const auto pid_file = directory / (mode + ".pid");
    const auto worker_stop = directory / (mode + ".worker.stop");
    const auto watchdog_stop = directory / (mode + ".watchdog.stop");
    const auto log = directory / (mode + ".jsonl");
    {
        std::ofstream control(watchdog_stop, std::ios::binary);
        control << "STOP\n";
    }
    const std::string fixture_command =
        quoted(fixture) + " " + mode + " " + quoted(pid_file) + " " + quoted(worker_stop);
    auto fixture_result = std::async(std::launch::async, [fixture_command] {
        return std::system(fixture_command.c_str());
    });
    const auto pid = await_pid(pid_file);
    const std::string watchdog_command =
        quoted(watchdog) + " --pid " + std::to_string(pid) +
        " --stop-file " + quoted(worker_stop) +
        " --watchdog-stop-file " + quoted(watchdog_stop) +
        " --log " + quoted(log) +
        " --campaign process-" + mode +
        " --interval-ms 20 --grace-ms 200";
    const int watchdog_result = std::system(watchdog_command.c_str());
    check(watchdog_result == 0, "independent watchdog command failed in " + mode + " mode");
    check(fixture_result.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
          "fixture remained alive after watchdog completion");
    const int worker_result = fixture_result.get();
    if (mode == "graceful") {
        check(worker_result == 0, "cooperative fixture did not exit cleanly");
    }
    check(std::filesystem::exists(worker_stop), "watchdog did not publish graceful stop file");
    std::ifstream events(log, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(events)), std::istreambuf_iterator<char>());
    check(content.find("graceful_stop_requested") != std::string::npos,
          "watchdog log lacks graceful-stop event");
    if (mode == "hang") {
        check(content.find("forced_stop") != std::string::npos, "watchdog log lacks forced-stop event");
    } else {
        check(content.find("worker_exited") != std::string::npos, "watchdog log lacks clean worker-exit event");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            throw std::invalid_argument("usage: watchdog-process-test WATCHDOG FIXTURE WORK_DIRECTORY");
        }
        const std::filesystem::path directory = argv[3];
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        exercise(argv[1], argv[2], directory, "graceful");
        exercise(argv[1], argv[2], directory, "hang");
        std::filesystem::remove_all(directory);
        std::cout << "primeforge-watchdog-process-tests: PASS\n";
        std::cout << "covered=independent cooperative stop, hung-worker forced stop, durable events\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-watchdog-process-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
