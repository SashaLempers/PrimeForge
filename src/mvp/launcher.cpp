// SPDX-License-Identifier: Apache-2.0

#include "primeforge/mvp/launcher.hpp"

#include "primeforge/mvp/search_config.hpp"
#include "primeforge/work/work_unit.hpp"

#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace primeforge::mvp {
namespace {

[[nodiscard]] bool regular_file(const std::filesystem::path& path) noexcept {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

[[nodiscard]] std::filesystem::path absolute_from(
    const std::filesystem::path& path,
    const std::filesystem::path& working_directory) {
    return (path.is_absolute() ? path : working_directory / path).lexically_normal();
}

}  // namespace

LauncherCampaignPaths make_launcher_campaign_paths(
    const std::filesystem::path& configuration,
    const std::filesystem::path& working_directory) {
    const auto absolute_working_directory = std::filesystem::absolute(working_directory);
    const auto absolute_configuration =
        absolute_from(configuration, absolute_working_directory);
    const auto config = load_search_config(absolute_configuration);
    const auto output = absolute_from(config.output_directory, absolute_working_directory);
    const auto stop_request =
        output.parent_path() / (output.filename().string() + ".launcher.stop");
    return {
        absolute_configuration,
        output,
        output / "campaign.checkpoint.json",
        output / "results.jsonl",
        output / "MANIFEST.sha256",
        stop_request,
    };
}

LauncherAction select_launcher_action(const LauncherCampaignPaths& paths) {
    const bool checkpoint = regular_file(paths.checkpoint);
    const bool results = regular_file(paths.results);
    const bool manifest = regular_file(paths.manifest);

    if (manifest) {
        if (!checkpoint || !results) {
            throw std::runtime_error(
                "completed campaign is missing its checkpoint or result ledger");
        }
        return LauncherAction::verify;
    }
    if (checkpoint) {
        if (!results) {
            throw std::runtime_error("checkpoint exists without its result ledger");
        }
        return LauncherAction::resume;
    }
    if (results || std::filesystem::exists(paths.output_directory)) {
        throw std::runtime_error(
            "campaign output exists without a checkpoint; refusing an ambiguous restart");
    }
    return LauncherAction::search;
}

std::string launcher_action_name(const LauncherAction action) {
    switch (action) {
    case LauncherAction::search: return "SEARCH";
    case LauncherAction::resume: return "RESUME";
    case LauncherAction::verify: return "VERIFY";
    }
    throw std::invalid_argument("unknown launcher action");
}

std::vector<std::string> make_launcher_arguments(
    const LauncherAction action,
    const LauncherCampaignPaths& paths,
    std::string prp_backend) {
    if (prp_backend != "auto" && prp_backend != "cpu" && prp_backend != "cuda") {
        throw std::invalid_argument("launcher PRP backend must be auto, cpu, or cuda");
    }
    switch (action) {
    case LauncherAction::search:
        return {"search", "--config", paths.configuration.string(), "--prp-backend",
                std::move(prp_backend), "--stop-file", paths.stop_request.string()};
    case LauncherAction::resume:
        return {"resume", "--checkpoint", paths.checkpoint.string(), "--prp-backend",
                std::move(prp_backend), "--stop-file", paths.stop_request.string()};
    case LauncherAction::verify:
        return {"verify", "--result", paths.results.string()};
    }
    throw std::invalid_argument("unknown launcher action");
}

std::filesystem::path archive_incompatible_campaign(
    const LauncherCampaignPaths& paths,
    const std::string_view unique_suffix) {
    if (!std::filesystem::is_directory(paths.output_directory)) {
        throw std::runtime_error("incompatible campaign directory is absent");
    }
    if (unique_suffix.empty() ||
        unique_suffix.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_") !=
            std::string_view::npos) {
        throw std::invalid_argument("campaign archive suffix is invalid");
    }
    const auto archived = paths.output_directory.parent_path() /
                          (paths.output_directory.filename().string() + ".incompatible-" +
                           std::string{unique_suffix});
    if (std::filesystem::exists(archived)) {
        throw std::runtime_error("campaign archive target already exists");
    }
    std::filesystem::rename(paths.output_directory, archived);
    return archived;
}

StopRequestFile::StopRequestFile(std::filesystem::path path) : path_{std::move(path)} {
    if (path_.empty() || !path_.has_filename()) {
        throw std::invalid_argument("stop-request file must name a file");
    }
}

const std::filesystem::path& StopRequestFile::path() const noexcept { return path_; }

bool StopRequestFile::requested() const noexcept { return regular_file(path_); }

void StopRequestFile::request() const {
    std::filesystem::create_directories(path_.parent_path());
    work::write_checkpoint_atomically(path_, "STOP\n");
}

void StopRequestFile::clear() const {
    std::error_code error;
    const bool removed = std::filesystem::remove(path_, error);
    static_cast<void>(removed);
    if (error) {
        throw std::system_error(error, "cannot clear stop-request file");
    }
}

}  // namespace primeforge::mvp
