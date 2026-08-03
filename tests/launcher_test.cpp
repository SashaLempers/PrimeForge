// SPDX-License-Identifier: Apache-2.0

#include "primeforge/mvp/launcher.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void touch(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary};
    if (!output) throw std::runtime_error("cannot create launcher test artifact");
    output << "fixture\n";
}

template <typename Function>
void expect_failure(Function&& function, const std::string& message) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 3) throw std::invalid_argument("configuration and test root required");
        const auto root = std::filesystem::absolute(argv[2]);
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        const auto paths = primeforge::mvp::make_launcher_campaign_paths(argv[1], root);

        check(primeforge::mvp::select_launcher_action(paths) ==
                  primeforge::mvp::LauncherAction::search,
              "empty campaign selects search");
        const auto search = primeforge::mvp::make_launcher_arguments(
            primeforge::mvp::LauncherAction::search, paths);
        check(search.size() == 7U && search[0] == "search" && search[5] == "--stop-file" &&
                  search[6] == paths.stop_request.string(),
              "search command carries the cooperative stop file");

        primeforge::mvp::StopRequestFile stop{paths.stop_request};
        check(!stop.requested(), "stop request starts clear");
        stop.request();
        check(stop.requested() && !std::filesystem::exists(paths.output_directory),
              "stop request becomes visible without creating the campaign directory");
        stop.clear();
        check(!stop.requested(), "stop request clears before restart");

        touch(paths.results);
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::select_launcher_action(paths)); },
            "result ledger without checkpoint is rejected");
        touch(paths.checkpoint);
        check(primeforge::mvp::select_launcher_action(paths) ==
                  primeforge::mvp::LauncherAction::resume,
              "checkpoint selects automatic resume");
        const auto resume = primeforge::mvp::make_launcher_arguments(
            primeforge::mvp::LauncherAction::resume, paths, "cuda");
        check(resume.size() == 7U && resume[0] == "resume" && resume[4] == "cuda",
              "resume preserves explicit backend and stop-file contract");

        touch(paths.manifest);
        check(primeforge::mvp::select_launcher_action(paths) ==
                  primeforge::mvp::LauncherAction::verify,
              "completed campaign selects verification");
        const auto verify = primeforge::mvp::make_launcher_arguments(
            primeforge::mvp::LauncherAction::verify, paths);
        check(verify.size() == 3U && verify[0] == "verify" &&
                  verify[2] == paths.results.string(),
              "completed campaign verifies its exact ledger");

        const auto archived = primeforge::mvp::archive_incompatible_campaign(
            paths, "20260803T200000Z");
        check(!std::filesystem::exists(paths.output_directory) &&
                  std::filesystem::is_regular_file(archived / "results.jsonl") &&
                  primeforge::mvp::select_launcher_action(paths) ==
                      primeforge::mvp::LauncherAction::search,
              "incompatible campaign is preserved and a clean search becomes selectable");
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::archive_incompatible_campaign(
                    paths, "../unsafe"));
            },
            "unsafe archive suffix is rejected");

        std::filesystem::rename(archived, paths.output_directory);

        std::filesystem::remove(paths.results);
        expect_failure(
            [&] { static_cast<void>(primeforge::mvp::select_launcher_action(paths)); },
            "incomplete finalized campaign is rejected");
        expect_failure(
            [&] {
                static_cast<void>(primeforge::mvp::make_launcher_arguments(
                    primeforge::mvp::LauncherAction::search, paths, "unknown"));
            },
            "unknown backend is rejected");

        std::filesystem::remove_all(root);
        std::cout << "launcher.search=PASS\n"
                  << "launcher.stop_request=PASS\n"
                  << "launcher.resume=PASS\n"
                  << "launcher.verify=PASS\n"
                  << "PrimeForge launcher tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge launcher tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
