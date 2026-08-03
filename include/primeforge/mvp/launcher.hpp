// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace primeforge::mvp {

enum class LauncherAction {
    search,
    resume,
    verify,
};

struct LauncherCampaignPaths {
    std::filesystem::path configuration;
    std::filesystem::path output_directory;
    std::filesystem::path checkpoint;
    std::filesystem::path results;
    std::filesystem::path manifest;
    std::filesystem::path stop_request;
};

[[nodiscard]] LauncherCampaignPaths make_launcher_campaign_paths(
    const std::filesystem::path& configuration,
    const std::filesystem::path& working_directory);

[[nodiscard]] LauncherAction select_launcher_action(
    const LauncherCampaignPaths& paths);

[[nodiscard]] std::string launcher_action_name(LauncherAction action);

[[nodiscard]] std::vector<std::string> make_launcher_arguments(
    LauncherAction action,
    const LauncherCampaignPaths& paths,
    std::string prp_backend = "auto");

[[nodiscard]] std::filesystem::path archive_incompatible_campaign(
    const LauncherCampaignPaths& paths,
    std::string_view unique_suffix);

class StopRequestFile final {
public:
    explicit StopRequestFile(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] bool requested() const noexcept;
    void request() const;
    void clear() const;

private:
    std::filesystem::path path_;
};

}  // namespace primeforge::mvp
