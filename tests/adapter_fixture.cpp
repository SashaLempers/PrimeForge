// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

[[nodiscard]] bool starts_with(
    const std::string_view value, const std::string_view prefix) noexcept {
    return value.starts_with(prefix);
}

[[nodiscard]] bool run_barrier(
    const std::string_view command, const std::string_view prefix) {
    const auto directory_end = command.find('|', prefix.size());
    const auto token_end = directory_end == std::string_view::npos
                               ? std::string_view::npos
                               : command.find('|', directory_end + 1U);
    const auto target_end = token_end == std::string_view::npos
                                ? std::string_view::npos
                                : command.find('|', token_end + 1U);
    if (directory_end == std::string_view::npos ||
        token_end == std::string_view::npos || target_end == std::string_view::npos) {
        return false;
    }
    const std::filesystem::path directory{
        std::string{command.substr(prefix.size(), directory_end - prefix.size())}};
    const std::string token{
        command.substr(directory_end + 1U, token_end - directory_end - 1U)};
    const auto target_text = command.substr(token_end + 1U, target_end - token_end - 1U);
    std::size_t target{};
    try {
        target = static_cast<std::size_t>(std::stoull(std::string{target_text}));
    } catch (const std::exception&) {
        return false;
    }
    if (target == 0U || token.empty()) return false;
    std::filesystem::create_directories(directory);
    std::ofstream ready{directory / (token + ".ready"), std::ios::binary | std::ios::trunc};
    ready << "READY\n";
    ready.close();
    if (!ready) return false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
        std::size_t ready_count{};
        for (const auto& entry : std::filesystem::directory_iterator{directory}) {
            if (entry.is_regular_file() && entry.path().extension() == ".ready") {
                ++ready_count;
            }
        }
        if (ready_count >= target) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return false;
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string_view command{argv[1]};
    if (argc == 2 && command == "SLEEP") {
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
        std::cout << "FIXTURE:PRP\n";
        return 0;
    }
    std::cerr << "FIXTURE_ARGUMENT_COUNT=" << argc - 1 << '\n';
    std::cerr << "FIXTURE_FIRST_ARGUMENT="
              << std::string_view{argv[1]}.substr(0U, 160U) << '\n';
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (starts_with(argument, "FAIL_PROCESS|")) return 17;
        if (starts_with(argument, "OMIT_OUTPUT|")) continue;
        if (starts_with(argument, "PADDED_PROVEN|")) {
            std::cout << "PROVEN_PRIME\n";
        } else if (starts_with(argument, "PADDED_COMPOSITE|")) {
            std::cout << "COMPOSITE\n";
        } else if (starts_with(argument, "BARRIER_PROVEN|")) {
            if (!run_barrier(argument, "BARRIER_PROVEN|")) {
                std::cerr << "FIXTURE_BARRIER_FAILED\n";
                return 18;
            }
            std::cout << "PROVEN_PRIME\n";
        } else if (starts_with(argument, "BARRIER_COMPOSITE|")) {
            if (!run_barrier(argument, "BARRIER_COMPOSITE|")) {
                std::cerr << "FIXTURE_BARRIER_FAILED\n";
                return 18;
            }
            std::cout << "COMPOSITE\n";
        } else {
            std::cout << argument << '\n';
        }
    }
    return 0;
}
