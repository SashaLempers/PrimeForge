// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/benchmark_logger.hpp"

#include "runtime_internal.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace primeforge::runtime {
namespace {

[[nodiscard]] std::uint64_t read_next_sequence(
    const std::filesystem::path& path,
    const std::string_view campaign_id) {
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0U) {
        return 1U;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot inspect benchmark log");
    }
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (content.empty() || content.back() != '\n') {
        throw std::runtime_error("benchmark log has an incomplete final record");
    }
    const auto previous_newline = content.rfind('\n', content.size() - 2U);
    const auto begin = previous_newline == std::string::npos ? 0U : previous_newline + 1U;
    const std::string_view last_line(content.data() + begin, content.size() - begin - 1U);
    const std::string expected_campaign = "\"campaign_id\":" + internal::json_escape(campaign_id);
    if (last_line.find(expected_campaign) == std::string_view::npos) {
        throw std::runtime_error("benchmark log campaign id does not match");
    }
    constexpr std::string_view marker = "\"sequence\":\"";
    const auto marker_position = last_line.find(marker);
    if (marker_position == std::string_view::npos) {
        throw std::runtime_error("benchmark log final record has no sequence");
    }
    const auto digits_begin = marker_position + marker.size();
    const auto digits_end = last_line.find('"', digits_begin);
    if (digits_end == std::string_view::npos) {
        throw std::runtime_error("benchmark log final sequence is truncated");
    }
    std::uint64_t sequence{};
    const auto parsed = std::from_chars(
        last_line.data() + digits_begin, last_line.data() + digits_end, sequence);
    if (parsed.ec != std::errc{} || parsed.ptr != last_line.data() + digits_end ||
        sequence == std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("benchmark log final sequence is invalid");
    }
    return sequence + 1U;
}

[[nodiscard]] std::FILE* open_append(const std::filesystem::path& path) {
#ifdef _WIN32
    std::FILE* file{};
    if (_wfopen_s(&file, path.c_str(), L"ab") != 0) {
        return nullptr;
    }
    return file;
#else
    return std::fopen(path.c_str(), "ab");
#endif
}

void durable_flush(std::FILE* file) {
    if (std::fflush(file) != 0) {
        throw std::runtime_error("cannot flush benchmark log");
    }
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0) {
#else
    if (fsync(fileno(file)) != 0) {
#endif
        throw std::runtime_error("cannot durably flush benchmark log");
    }
}

} // namespace

BenchmarkLogger::BenchmarkLogger(std::filesystem::path path, std::string campaign_id)
    : path_(std::move(path)), campaign_id_(std::move(campaign_id)) {
    if (campaign_id_.empty()) {
        throw std::invalid_argument("campaign id must not be empty");
    }
    if (!path_.has_filename()) {
        throw std::invalid_argument("benchmark log path must name a file");
    }
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    next_sequence_ = read_next_sequence(path_, campaign_id_);
    file_ = open_append(path_);
    if (file_ == nullptr) {
        throw std::runtime_error("cannot open benchmark log");
    }
}

BenchmarkLogger::~BenchmarkLogger() {
    if (file_ != nullptr) {
        std::fclose(file_);
    }
}

void BenchmarkLogger::append(
    const std::string_view event_type,
    const std::string_view payload_json,
    const std::string_view utc) {
    if (event_type.empty() || utc.empty()) {
        throw std::invalid_argument("event type and UTC timestamp must not be empty");
    }
    if (payload_json.size() < 2U || payload_json.front() != '{' || payload_json.back() != '}' ||
        payload_json.find_first_of("\r\n") != std::string_view::npos) {
        throw std::invalid_argument("payload must be one complete JSON object on one line");
    }
    const std::string record =
        "{\"campaign_id\":" + internal::json_escape(campaign_id_) +
        ",\"event_type\":" + internal::json_escape(event_type) +
        ",\"payload\":" + std::string(payload_json) +
        ",\"sequence\":" + internal::json_escape(std::to_string(next_sequence_)) +
        ",\"utc\":" + internal::json_escape(utc) + "}\n";
    if (std::fwrite(record.data(), 1U, record.size(), file_) != record.size()) {
        throw std::runtime_error("cannot append benchmark log record");
    }
    durable_flush(file_);
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("benchmark log sequence exhausted");
    }
    ++next_sequence_;
}

std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setw(3) << std::setfill('0') << milliseconds.count() << 'Z';
    return result.str();
}

} // namespace primeforge::runtime
