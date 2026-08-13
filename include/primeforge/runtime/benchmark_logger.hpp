// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace primeforge::runtime {

class BenchmarkLogger {
public:
    BenchmarkLogger(std::filesystem::path path, std::string campaign_id, bool enabled = true);
    ~BenchmarkLogger();

    BenchmarkLogger(const BenchmarkLogger&) = delete;
    BenchmarkLogger& operator=(const BenchmarkLogger&) = delete;

    void append(std::string_view event_type, std::string_view payload_json, std::string_view utc);
    [[nodiscard]] std::uint64_t next_sequence() const noexcept { return next_sequence_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    std::string campaign_id_;
    std::FILE* file_{};
    bool enabled_{true};
    std::uint64_t next_sequence_{1U};
};

[[nodiscard]] std::string utc_now();

} // namespace primeforge::runtime
