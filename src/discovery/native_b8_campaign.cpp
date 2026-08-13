// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/native_b8_campaign.hpp"

#include "primeforge/work/work_unit.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace primeforge::discovery::native_b8 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view telemetry_schema = "primeforge.campaign.telemetry.v1";

const std::vector<std::string>& telemetry_columns() {
    static const std::vector<std::string> columns{
        "schema_version", "utc_timestamp", "monotonic_ns", "campaign_id", "record_type", "scope",
        "batch_id", "batch_size", "lane_id", "k", "n", "classification", "witness", "RES64",
        "validation_status", "candidate_result_hash", "ordered_candidate_hash", "engine_hash",
        "terminal_status", "result_hash", "scheduler_prepare_us", "parameter_build_us",
        "host_to_device_us", "witness_selection_us", "a_pow_k_gpu_us", "main_ntt_gpu_us",
        "gerbicz_gpu_us", "final_reduce_gpu_us", "device_to_host_us", "result_parse_us",
        "result_persist_us", "checkpoint_us", "worker_total_us", "batch_wall_us", "derived_average_us", "gpu_name",
        "gpu_uuid", "gpu_util_percent", "gpu_memory_controller_percent", "gpu_power_w",
        "gpu_energy_wh_integrated", "gpu_temperature_c", "gpu_hotspot_c", "gpu_memory_temperature_c",
        "gpu_core_clock_mhz", "gpu_memory_clock_mhz", "gpu_vram_used_bytes", "gpu_vram_total_bytes",
        "gpu_pcie_tx_bytes_per_s", "gpu_pcie_rx_bytes_per_s", "gpu_throttle_reasons",
        "process_cpu_percent", "process_cpu_time_ms", "system_cpu_percent", "scheduler_thread_cpu_ms",
        "process_working_set_bytes", "process_private_bytes", "system_available_ram_bytes",
        "survivor_queue_bytes", "telemetry_buffer_bytes", "process_read_bytes", "process_write_bytes",
        "checkpoint_bytes_written", "telemetry_bytes_written", "active_batch_id", "active_batch_size",
        "completed_unique", "remaining", "watchdog_state", "WHEA_delta", "Xid_delta", "error_code",
        "message"};
    return columns;
}

[[nodiscard]] std::uint64_t monotonic_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count());
}

[[nodiscard]] std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm value{};
#if defined(_WIN32)
    gmtime_s(&value, &time);
#else
    gmtime_r(&time, &value);
#endif
    std::ostringstream output;
    output << std::put_time(&value, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setw(3) << std::setfill('0') << milliseconds.count() << 'Z';
    return output.str();
}

[[nodiscard]] std::string eta_utc(const std::size_t remaining, const double throughput) {
    if (remaining == 0U) { return utc_now(); }
    if (!std::isfinite(throughput) || throughput <= 0.0) { return "UNKNOWN"; }
    const auto seconds = static_cast<std::int64_t>(
        std::ceil(static_cast<double>(remaining) * 3600.0 / throughput));
    const auto target = std::chrono::system_clock::now() + std::chrono::seconds{seconds};
    const std::time_t time = std::chrono::system_clock::to_time_t(target);
    std::tm value{};
#if defined(_WIN32)
    gmtime_s(&value, &time);
#else
    gmtime_r(&time, &value);
#endif
    std::ostringstream output;
    output << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

[[nodiscard]] std::string tsv_escape(std::string value) {
    std::ranges::replace(value, '\t', ' ');
    std::ranges::replace(value, '\r', ' ');
    std::ranges::replace(value, '\n', ' ');
    return value;
}

[[nodiscard]] std::vector<std::string> split(std::string_view value, char separator);

[[nodiscard]] std::optional<std::string> last_complete_tsv_value(
    const std::filesystem::path& path,
    const std::string_view column) {
    if (!std::filesystem::exists(path)) { return std::nullopt; }
    std::ifstream input(path, std::ios::binary);
    std::string schema;
    std::string header;
    if (!std::getline(input, schema) || !std::getline(input, header)) { return std::nullopt; }
    if (!header.empty() && header.back() == '\r') { header.pop_back(); }
    const auto columns = split(header, '\t');
    const auto found = std::ranges::find(columns, column);
    if (found == columns.end()) { return std::nullopt; }
    const auto index = static_cast<std::size_t>(std::distance(columns.begin(), found));
    std::optional<std::string> result;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        const auto fields = split(line, '\t');
        if (fields.size() == columns.size() && !fields[index].empty() && fields[index] != "UNKNOWN") {
            result = fields[index];
        }
    }
    return result;
}

[[nodiscard]] std::optional<double> finite_number(const std::string_view text) {
    try {
        std::size_t consumed{};
        const auto value = std::stod(std::string{text}, &consumed);
        if (consumed == text.size() && std::isfinite(value) && value >= 0.0) { return value; }
    } catch (...) {}
    return std::nullopt;
}

[[nodiscard]] std::string decimal_number(const double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(9) << value;
    return output.str();
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result{"\""};
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (byte < 0x20U) {
                result += "\\u00";
                result.push_back(hex[byte >> 4U]);
                result.push_back(hex[byte & 0x0fU]);
            } else {
                result.push_back(static_cast<char>(byte));
            }
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] std::string hash_bytes(
    const std::string_view value, const Sha256Provider& sha256) {
    return sha256_to_hex(sha256.digest(std::as_bytes(std::span{value.data(), value.size()})));
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { throw std::runtime_error("cannot read file: " + path.string()); }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void discard_truncated_final_line(const std::filesystem::path& path) {
    const auto content = read_file(path);
    if (content.empty() || content.back() == '\n') { return; }
    const auto newline = content.find_last_of('\n');
    if (newline == std::string::npos) {
        throw std::runtime_error("campaign telemetry has no complete schema line");
    }
    std::filesystem::resize_file(path, newline + 1U);
}

void sync_file(const std::filesystem::path& path) {
#if defined(_WIN32)
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("cannot open telemetry for durable flush");
    }
    if (FlushFileBuffers(handle) == 0) {
        CloseHandle(handle);
        throw std::runtime_error("cannot durably flush telemetry");
    }
    CloseHandle(handle);
#else
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) { throw std::runtime_error("cannot open telemetry for durable flush"); }
    if (::fsync(descriptor) != 0) {
        ::close(descriptor);
        throw std::runtime_error("cannot durably flush telemetry");
    }
    ::close(descriptor);
#endif
}

[[nodiscard]] std::vector<std::string> split(const std::string_view value, const char separator) {
    std::vector<std::string> fields;
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const auto end = value.find(separator, begin);
        fields.emplace_back(value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin));
        if (end == std::string_view::npos) { break; }
        begin = end + 1U;
    }
    return fields;
}

template <typename Integer>
[[nodiscard]] Integer parse_unsigned(const std::string_view text, const char* field) {
    Integer value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::runtime_error(std::string{"invalid unsigned integer for "} + field);
    }
    return value;
}

[[nodiscard]] std::optional<std::string> json_string_field(
    const std::string_view json, const std::string_view name) {
    const std::string marker = "\"" + std::string{name} + "\":";
    const auto position = json.find(marker);
    if (position == std::string_view::npos || json.find(marker, position + marker.size()) != std::string_view::npos) {
        return std::nullopt;
    }
    auto begin = position + marker.size();
    if (begin >= json.size() || json[begin] != '"') { return std::nullopt; }
    ++begin;
    std::string result;
    bool escaped = false;
    for (auto index = begin; index < json.size(); ++index) {
        const char character = json[index];
        if (escaped) {
            if (character != '"' && character != '\\') { return std::nullopt; }
            result.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return result;
        } else {
            result.push_back(character);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string require_json_string(
    const std::string_view json, const std::string_view name) {
    const auto value = json_string_field(json, name);
    if (!value) { throw std::runtime_error("checkpoint field is missing or malformed: " + std::string{name}); }
    return *value;
}

[[nodiscard]] std::string canonical_candidates(const std::vector<Candidate>& candidates) {
    std::string content;
    for (const auto& candidate : candidates) {
        content += std::to_string(candidate.k) + "\t" + std::to_string(candidate.n) + "\n";
    }
    return content;
}

[[nodiscard]] std::string result_hash_input(
    const std::string_view campaign_id,
    const std::uint64_t batch_id,
    const CandidateResult& result) {
    return std::string{campaign_id} + "\t" + std::to_string(batch_id) + "\t" +
        std::to_string(result.lane_id) + "\t" + std::to_string(result.candidate.k) + "\t" +
        std::to_string(result.candidate.n) + "\t" + result.classification + "\t" +
        std::to_string(result.witness) + "\t" + result.res64 + "\t" + result.validation_status + "\n";
}

struct StoredResult {
    std::string campaign_id;
    std::uint64_t batch_id{};
    std::string ordered_candidate_hash;
    std::size_t batch_size{};
    CandidateResult result;
    std::string engine_hash;
    std::string start_utc;
    std::string terminal_status;
    std::string batch_result_hash;
};

constexpr std::string_view results_header =
    "campaign_id\tbatch_id\tordered_candidate_hash\tbatch_size\tlane_id\tk\tn\tengine_hash\tstart_utc\t"
    "terminal_status\tclassification\twitness\tRES64\tvalidation_status\tcandidate_result_hash\tbatch_result_hash\n";

[[nodiscard]] std::string serialize_results(const std::vector<StoredResult>& results) {
    std::string content{results_header};
    for (const auto& stored : results) {
        const auto& result = stored.result;
        content += stored.campaign_id + "\t" + std::to_string(stored.batch_id) + "\t" +
            stored.ordered_candidate_hash + "\t" + std::to_string(stored.batch_size) + "\t" +
            std::to_string(result.lane_id) + "\t" + std::to_string(result.candidate.k) + "\t" +
            std::to_string(result.candidate.n) + "\t" + stored.engine_hash + "\t" + stored.start_utc + "\t" +
            stored.terminal_status + "\t" + result.classification + "\t" + std::to_string(result.witness) +
            "\t" + result.res64 + "\t" + result.validation_status + "\t" +
            result.candidate_result_hash + "\t" + stored.batch_result_hash + "\n";
    }
    return content;
}

[[nodiscard]] std::vector<StoredResult> read_results(
    const std::filesystem::path& path,
    const CampaignConfig& config,
    const Sha256Provider& sha256) {
    if (!std::filesystem::exists(path)) { return {}; }
    const auto content = read_file(path);
    if (!content.starts_with(results_header)) { throw std::runtime_error("candidate results header mismatch"); }
    std::vector<StoredResult> results;
    std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
    std::istringstream input(content.substr(results_header.size()));
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) { continue; }
        const auto fields = split(line, '\t');
        if (fields.size() != 16U) { throw std::runtime_error("malformed candidate results row"); }
        StoredResult stored;
        stored.campaign_id = fields[0];
        stored.batch_id = parse_unsigned<std::uint64_t>(fields[1], "batch_id");
        stored.ordered_candidate_hash = fields[2];
        stored.batch_size = parse_unsigned<std::size_t>(fields[3], "batch_size");
        stored.result.lane_id = parse_unsigned<std::size_t>(fields[4], "lane_id");
        stored.result.candidate.k = parse_unsigned<std::uint32_t>(fields[5], "k");
        stored.result.candidate.n = parse_unsigned<std::uint32_t>(fields[6], "n");
        stored.engine_hash = fields[7];
        stored.start_utc = fields[8];
        stored.terminal_status = fields[9];
        stored.result.classification = fields[10];
        stored.result.witness = parse_unsigned<std::uint32_t>(fields[11], "witness");
        stored.result.res64 = fields[12];
        stored.result.validation_status = fields[13];
        stored.result.candidate_result_hash = fields[14];
        stored.batch_result_hash = fields[15];
        if (stored.campaign_id != config.campaign_id || stored.engine_hash != config.engine_binary_sha256 ||
            stored.batch_size == 0U || stored.batch_size > config.batch_size ||
            stored.result.lane_id >= stored.batch_size ||
            (stored.result.classification != "COMPOSITE" && stored.result.classification != "PROVEN_PRIME") ||
            stored.result.validation_status != "GERBICZ_PASS" || stored.result.res64.size() != 16U) {
            throw std::runtime_error("candidate results identity or verdict mismatch");
        }
        const auto expected_hash = hash_bytes(
            result_hash_input(config.campaign_id, stored.batch_id, stored.result), sha256);
        if (stored.result.candidate_result_hash != expected_hash) {
            throw std::runtime_error("candidate result hash mismatch");
        }
        if (!seen.emplace(stored.result.candidate.k, stored.result.candidate.n).second) {
            throw std::runtime_error("duplicate durable candidate result");
        }
        results.push_back(std::move(stored));
    }
    return results;
}

[[nodiscard]] std::string batch_hash(
    const BatchRequest& request,
    const std::vector<CandidateResult>& results,
    const std::string_view engine_hash,
    const Sha256Provider& sha256) {
    std::string content = std::to_string(request.batch_id) + "\t" + request.ordered_candidate_hash + "\t" +
        std::to_string(request.candidates.size()) + "\t" + std::string{engine_hash} + "\n";
    for (const auto& result : results) { content += result.candidate_result_hash + "\n"; }
    return hash_bytes(content, sha256);
}

struct DurableResultState {
    std::uint64_t next_batch_id{};
    std::vector<std::size_t> batch_end_offsets{0U};
};

[[nodiscard]] DurableResultState validate_result_batches(
    const std::vector<StoredResult>& results,
    const Sha256Provider& sha256) {
    DurableResultState state;
    std::size_t offset{};
    while (offset < results.size()) {
        const auto& first = results[offset];
        if (first.batch_id != state.next_batch_id || first.batch_size == 0U ||
            first.batch_size > 8U || results.size() - offset < first.batch_size) {
            throw std::runtime_error("durable result batches are not contiguous and complete");
        }
        BatchRequest request;
        request.batch_id = first.batch_id;
        request.ordered_candidate_hash = first.ordered_candidate_hash;
        std::vector<CandidateResult> batch_results;
        bool has_prime = false;
        for (std::size_t lane = 0U; lane < first.batch_size; ++lane) {
            const auto& stored = results[offset + lane];
            if (stored.batch_id != first.batch_id || stored.batch_size != first.batch_size ||
                stored.result.lane_id != lane || stored.ordered_candidate_hash != first.ordered_candidate_hash ||
                stored.engine_hash != first.engine_hash || stored.start_utc != first.start_utc ||
                stored.terminal_status != "BATCH_COMPLETE" ||
                stored.batch_result_hash != first.batch_result_hash) {
                throw std::runtime_error("durable result batch metadata is inconsistent");
            }
            request.candidates.push_back(stored.result.candidate);
            batch_results.push_back(stored.result);
            has_prime = has_prime || stored.result.classification == "PROVEN_PRIME";
        }
        if (ordered_batch_sha256(request.candidates, sha256) != first.ordered_candidate_hash ||
            batch_hash(request, batch_results, first.engine_hash, sha256) != first.batch_result_hash) {
            throw std::runtime_error("durable result batch hash mismatch");
        }
        offset += first.batch_size;
        ++state.next_batch_id;
        state.batch_end_offsets.push_back(offset);
        if (has_prime && offset != results.size()) {
            throw std::runtime_error("durable results continue after a proven-prime batch");
        }
    }
    return state;
}

void validate_checkpoint(
    const std::filesystem::path& path,
    const CampaignConfig& config,
    const std::vector<StoredResult>& results,
    const DurableResultState& result_state,
    const std::vector<Candidate>& parent_completed,
    const std::vector<Candidate>& queue,
    const Sha256Provider& sha256) {
    if (!std::filesystem::exists(path)) {
        // A crash can occur after the atomically durable result file is replaced but before
        // its matching checkpoint is replaced. Fully validated complete result batches are
        // therefore a recoverable write-ahead state, including for the first batch.
        return;
    }
    const auto checkpoint = read_file(path);
    if (require_json_string(checkpoint, "schema_version") != "1" ||
        require_json_string(checkpoint, "campaign_id") != config.campaign_id ||
        require_json_string(checkpoint, "parent_campaign_id") != config.parent_campaign_id ||
        require_json_string(checkpoint, "engine_commit") != config.engine_commit ||
        require_json_string(checkpoint, "engine_binary_sha256") != config.engine_binary_sha256 ||
        require_json_string(checkpoint, "survivor_list_sha256") != config.survivor_list_sha256) {
        throw std::runtime_error("checkpoint identity mismatch");
    }
    const auto terminal = require_json_string(checkpoint, "terminal_status");
    if (terminal != "IN_PROGRESS" && terminal != "STOPPED") {
        throw std::runtime_error("campaign checkpoint is terminal or unsafe to resume: " + terminal);
    }
    const auto checkpoint_next = parse_unsigned<std::uint64_t>(
        require_json_string(checkpoint, "next_batch_id"), "next_batch_id");
    if (checkpoint_next > result_state.next_batch_id ||
        checkpoint_next >= result_state.batch_end_offsets.size()) {
        throw std::runtime_error("checkpoint is ahead of durable complete result batches");
    }
    const auto resume_count = result_state.batch_end_offsets[static_cast<std::size_t>(checkpoint_next)];
    std::vector<StoredResult> prefix(results.begin(), results.begin() + static_cast<std::ptrdiff_t>(resume_count));
    auto completed = parent_completed;
    for (const auto& stored : prefix) { completed.push_back(stored.result.candidate); }
    const std::vector<Candidate> checkpoint_remaining(
        queue.begin() + static_cast<std::ptrdiff_t>(resume_count), queue.end());
    const auto expected_last_id = checkpoint_next == 0U ? 0U : checkpoint_next - 1U;
    const auto expected_last_hash = checkpoint_next == 0U
        ? std::string(64U, '0') : prefix.back().batch_result_hash;
    if (parse_unsigned<std::size_t>(
            require_json_string(checkpoint, "completed_unique_count"), "completed_unique_count") != completed.size() ||
        require_json_string(checkpoint, "candidate_results_hash") != hash_bytes(serialize_results(prefix), sha256) ||
        require_json_string(checkpoint, "completed_set_sha256") != candidate_set_sha256(completed, sha256) ||
        require_json_string(checkpoint, "remaining_queue_sha256") != candidate_set_sha256(checkpoint_remaining, sha256) ||
        parse_unsigned<std::uint64_t>(require_json_string(checkpoint, "last_batch_id"), "last_batch_id") != expected_last_id ||
        require_json_string(checkpoint, "last_batch_hash") != expected_last_hash) {
        throw std::runtime_error("checkpoint content hash or count mismatch");
    }
    static_cast<void>(require_json_string(checkpoint, "updated_utc"));
}

[[nodiscard]] std::vector<Candidate> resume_completed_candidates(const std::vector<StoredResult>& results) {
    std::vector<Candidate> candidates;
    candidates.reserve(results.size());
    for (const auto& result : results) { candidates.push_back(result.result.candidate); }
    return candidates;
}

[[nodiscard]] std::string checkpoint_json(
    const CampaignConfig& config,
    const std::vector<Candidate>& all_completed,
    const std::vector<Candidate>& remaining,
    const std::string_view results_hash,
    const std::uint64_t next_batch_id,
    const std::uint64_t last_batch_id,
    const std::string_view last_batch_hash,
    const std::string_view terminal_status,
    const Sha256Provider& sha256,
    const std::string_view updated) {
    return "{\"campaign_id\":" + json_escape(config.campaign_id) +
        ",\"candidate_results_hash\":" + json_escape(results_hash) +
        ",\"completed_set_sha256\":" + json_escape(candidate_set_sha256(all_completed, sha256)) +
        ",\"completed_unique_count\":" + json_escape(std::to_string(all_completed.size())) +
        ",\"engine_binary_sha256\":" + json_escape(config.engine_binary_sha256) +
        ",\"engine_commit\":" + json_escape(config.engine_commit) +
        ",\"last_batch_hash\":" + json_escape(last_batch_hash) +
        ",\"last_batch_id\":" + json_escape(std::to_string(last_batch_id)) +
        ",\"next_batch_id\":" + json_escape(std::to_string(next_batch_id)) +
        ",\"parent_campaign_id\":" + json_escape(config.parent_campaign_id) +
        ",\"remaining_queue_sha256\":" + json_escape(candidate_set_sha256(remaining, sha256)) +
        ",\"schema_version\":\"1\""
        ",\"survivor_list_sha256\":" + json_escape(config.survivor_list_sha256) +
        ",\"terminal_status\":" + json_escape(terminal_status) +
        ",\"updated_utc\":" + json_escape(updated) + "}\n";
}

[[nodiscard]] std::string optional_decimal(const std::optional<std::uint64_t>& value) {
    return value ? std::to_string(*value) : "UNKNOWN";
}

void require_sha256(const std::string_view value, const char* field) {
    if (value.size() != 64U || !std::ranges::all_of(value, [](const unsigned char character) {
            return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        })) {
        throw std::invalid_argument(std::string{field} + " must be lowercase SHA-256 hex");
    }
}

}  // namespace

TelemetryWriter::TelemetryWriter(
    std::filesystem::path path,
    std::string campaign_id,
    const TelemetryLimits limits)
    : path_(std::move(path)), campaign_id_(std::move(campaign_id)), limits_(limits) {
    if (campaign_id_.empty() || path_.filename().empty() || limits_.soft_limit_bytes == 0U ||
        limits_.hard_limit_bytes <= limits_.soft_limit_bytes ||
        limits_.normal_resource_interval_seconds == 0U || limits_.reduced_resource_interval_seconds == 0U) {
        throw std::invalid_argument("invalid campaign telemetry configuration");
    }
    if (!std::filesystem::exists(path_)) {
        std::string header = "#schema\t" + std::string{telemetry_schema} + "\n";
        const auto& columns = telemetry_columns();
        for (std::size_t index = 0U; index < columns.size(); ++index) {
            if (index != 0U) { header.push_back('\t'); }
            header += columns[index];
        }
        header.push_back('\n');
        work::write_checkpoint_atomically(path_, header);
    } else {
        discard_truncated_final_line(path_);
        if (!valid_stream_prefix(path_)) {
            throw std::runtime_error("campaign telemetry prefix is malformed");
        }
    }
    persisted_bytes_ = std::filesystem::file_size(path_);
    resource_samples_enabled_ = persisted_bytes_ < limits_.hard_limit_bytes;
    last_flush_monotonic_ns_ = monotonic_ns();
}

TelemetryWriter::~TelemetryWriter() {
    try { flush(false); } catch (...) {}
}

bool TelemetryWriter::append(
    const std::string_view record_type,
    const std::string_view scope,
    const TelemetryFields& fields,
    const bool essential) {
    if (record_type.empty() || scope.empty()) { throw std::invalid_argument("telemetry record identity is required"); }
    if (record_type == "RESOURCE_SAMPLE" && !resource_samples_enabled_) { return false; }
    const auto now = monotonic_ns();
    if (record_type == "RESOURCE_SAMPLE") {
        const auto found = last_resource_sample_by_scope_.find(std::string{scope});
        if (found != last_resource_sample_by_scope_.end() &&
            now - found->second < resource_interval_seconds() * 1'000'000'000ULL) {
            return false;
        }
    }
    TelemetryFields values = fields;
    values["schema_version"] = "1";
    values["utc_timestamp"] = utc_now();
    values["monotonic_ns"] = std::to_string(now);
    values["campaign_id"] = campaign_id_;
    values["record_type"] = std::string{record_type};
    values["scope"] = std::string{scope};
    std::string line;
    const auto& columns = telemetry_columns();
    for (std::size_t index = 0U; index < columns.size(); ++index) {
        if (index != 0U) { line.push_back('\t'); }
        const auto found = values.find(columns[index]);
        if (found != values.end()) { line += tsv_escape(found->second); }
    }
    line.push_back('\n');
    const auto projected = persisted_bytes_ + buffer_.size() + line.size();
    if (record_type == "RESOURCE_SAMPLE" && projected >= limits_.hard_limit_bytes) {
        resource_samples_enabled_ = false;
        return false;
    }
    if (record_type == "RESOURCE_SAMPLE") { last_resource_sample_by_scope_[std::string{scope}] = now; }
    if (!essential && record_type == "RESOURCE_SAMPLE" && projected >= limits_.soft_limit_bytes) {
        // The caller observes resource_interval_seconds() and slows future sampling.
    }
    buffer_ += line;
    if (buffer_.size() >= 64U * 1024U || now - last_flush_monotonic_ns_ >= 5'000'000'000ULL) {
        flush(false);
    }
    return true;
}

void TelemetryWriter::flush(const bool durable) {
    if (!buffer_.empty()) {
        std::ofstream output(path_, std::ios::binary | std::ios::app);
        if (!output) { throw std::runtime_error("cannot append campaign telemetry"); }
        output.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
        output.flush();
        if (!output) { throw std::runtime_error("cannot flush campaign telemetry"); }
        persisted_bytes_ += buffer_.size();
        buffer_.clear();
    }
    if (durable) { sync_file(path_); }
    last_flush_monotonic_ns_ = monotonic_ns();
    if (persisted_bytes_ >= limits_.hard_limit_bytes) { resource_samples_enabled_ = false; }
}

std::uint64_t TelemetryWriter::bytes_written() const noexcept { return persisted_bytes_ + buffer_.size(); }
std::size_t TelemetryWriter::buffered_bytes() const noexcept { return buffer_.size(); }
std::uint64_t TelemetryWriter::resource_interval_seconds() const noexcept {
    return bytes_written() >= limits_.soft_limit_bytes
        ? limits_.reduced_resource_interval_seconds : limits_.normal_resource_interval_seconds;
}
bool TelemetryWriter::resource_samples_enabled() const noexcept { return resource_samples_enabled_; }

bool TelemetryWriter::valid_stream_prefix(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { return false; }
    const std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    const auto first_end = content.find('\n');
    const auto second_end = first_end == std::string::npos ? std::string::npos : content.find('\n', first_end + 1U);
    if (first_end == std::string::npos || second_end == std::string::npos ||
        content.substr(0U, first_end) != "#schema\t" + std::string{telemetry_schema}) {
        return false;
    }
    std::string expected_header;
    const auto& columns = telemetry_columns();
    for (std::size_t index = 0U; index < columns.size(); ++index) {
        if (index != 0U) { expected_header.push_back('\t'); }
        expected_header += columns[index];
    }
    if (content.substr(first_end + 1U, second_end - first_end - 1U) != expected_header) { return false; }
    const auto expected_tabs = columns.size() - 1U;
    std::size_t begin = second_end + 1U;
    while (begin < content.size()) {
        const auto end = content.find('\n', begin);
        if (end == std::string::npos) { break; }
        const auto line = std::string_view{content}.substr(begin, end - begin);
        if (!line.empty() && static_cast<std::size_t>(std::ranges::count(line, '\t')) != expected_tabs) {
            return false;
        }
        begin = end + 1U;
    }
    return true;
}

std::vector<Candidate> read_candidate_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { throw std::runtime_error("cannot read candidate file: " + path.string()); }
    std::vector<Candidate> candidates;
    std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        if (line.empty()) { continue; }
        const auto fields = split(line, line.find('\t') != std::string::npos ? '\t' : ' ');
        if (fields.size() != 2U || fields[0].empty() || fields[1].empty()) {
            throw std::runtime_error("malformed candidate line");
        }
        Candidate candidate{
            parse_unsigned<std::uint32_t>(fields[0], "k"),
            parse_unsigned<std::uint32_t>(fields[1], "n")};
        if (candidate.k < 3U || candidate.k >= 100'000'000U || (candidate.k & 1U) == 0U || candidate.n < 32U ||
            !seen.emplace(candidate.k, candidate.n).second) {
            throw std::runtime_error("invalid or duplicate candidate");
        }
        candidates.push_back(candidate);
    }
    return candidates;
}

std::string candidate_set_sha256(
    const std::vector<Candidate>& candidates,
    const Sha256Provider& sha256) {
    auto ordered = candidates;
    std::ranges::sort(ordered, {}, [](const Candidate& candidate) {
        return std::pair{candidate.k, candidate.n};
    });
    if (std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end()) {
        throw std::runtime_error("candidate set contains duplicates");
    }
    return hash_bytes(canonical_candidates(ordered), sha256);
}

std::string ordered_batch_sha256(
    const std::vector<Candidate>& candidates,
    const Sha256Provider& sha256) {
    if (candidates.empty() || candidates.size() > 8U) {
        throw std::invalid_argument("native batch must contain one to eight candidates");
    }
    return hash_bytes(canonical_candidates(candidates), sha256);
}

BatchExecution parse_worker_output(
    const std::string_view output,
    const std::vector<Candidate>& expected,
    const int worker_exit_code) {
    const auto parse_start = Clock::now();
    if (worker_exit_code != 0) { throw std::runtime_error("native B8 worker exited nonzero"); }
    if (expected.empty() || expected.size() > 8U) { throw std::invalid_argument("expected native batch size is invalid"); }
    BatchExecution execution;
    execution.worker_exit_code = worker_exit_code;
    std::map<std::string, std::string, std::less<>> timing;
    std::istringstream input{std::string{output}};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        constexpr std::string_view result_marker = "PRIMEFORGE_NATIVE_BATCH_RESULT\t";
        constexpr std::string_view timing_marker = "PRIMEFORGE_NATIVE_BATCH_TIMING\t";
        if (line.starts_with(result_marker)) {
            const auto fields = split(std::string_view{line}.substr(result_marker.size()), '\t');
            if (fields.size() != 6U) { throw std::runtime_error("malformed native batch result marker"); }
            CandidateResult result;
            result.lane_id = parse_unsigned<std::size_t>(fields[0], "lane_id");
            result.candidate.k = parse_unsigned<std::uint32_t>(fields[1], "k");
            result.candidate.n = parse_unsigned<std::uint32_t>(fields[2], "n");
            result.witness = parse_unsigned<std::uint32_t>(fields[3], "witness");
            result.classification = fields[4];
            result.res64 = fields[5];
            if (result.lane_id >= expected.size() || result.candidate != expected[result.lane_id] ||
                (result.classification != "COMPOSITE" && result.classification != "PROVEN_PRIME") ||
                result.witness < 3U || result.res64.size() != 16U ||
                !std::ranges::all_of(result.res64, [](const unsigned char value) {
                    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F');
                })) {
                throw std::runtime_error("native batch result identity or value mismatch");
            }
            execution.results.push_back(std::move(result));
        } else if (line.starts_with(timing_marker)) {
            if (!timing.empty()) { throw std::runtime_error("duplicate native batch timing marker"); }
            for (const auto& field : split(std::string_view{line}.substr(timing_marker.size()), '\t')) {
                const auto equals = field.find('=');
                if (equals == std::string::npos || equals == 0U) { throw std::runtime_error("malformed timing field"); }
                if (!timing.emplace(field.substr(0U, equals), field.substr(equals + 1U)).second) {
                    throw std::runtime_error("duplicate timing field");
                }
            }
        } else if (line == "PRIMEFORGE_NATIVE_BATCH_STOPPED") {
            execution.stopped = true;
        }
    }
    if (execution.stopped) {
        if (!execution.results.empty()) { throw std::runtime_error("stopped native batch emitted partial results"); }
        execution.result_parse_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - parse_start).count());
        return execution;
    }
    if (execution.results.size() != expected.size()) { throw std::runtime_error("native batch result count mismatch"); }
    std::ranges::sort(execution.results, {}, &CandidateResult::lane_id);
    for (std::size_t lane = 0U; lane < execution.results.size(); ++lane) {
        if (execution.results[lane].lane_id != lane) { throw std::runtime_error("native batch lane is missing or duplicated"); }
    }
    const auto timing_value = [&](const char* name) -> std::optional<std::uint64_t> {
        const auto found = timing.find(name);
        if (found == timing.end()) { return std::nullopt; }
        return parse_unsigned<std::uint64_t>(found->second, name);
    };
    if (timing.find("gerbicz_status") == timing.end() || timing.at("gerbicz_status") != "PASS") {
        throw std::runtime_error("native batch Gerbicz status is missing or failed");
    }
    execution.phases.parameter_build_us = timing_value("parameter_build_us");
    execution.phases.host_to_device_us = timing_value("host_to_device_us");
    execution.phases.witness_selection_us = timing_value("witness_selection_us");
    execution.phases.a_pow_k_gpu_us = timing_value("a_pow_k_gpu_us");
    execution.phases.main_ntt_gpu_us = timing_value("main_ntt_gpu_us");
    execution.phases.gerbicz_gpu_us = timing_value("gerbicz_gpu_us");
    execution.phases.final_reduce_gpu_us = timing_value("final_reduce_gpu_us");
    execution.phases.device_to_host_us = timing_value("device_to_host_us");
    execution.phases.worker_total_us = timing_value("worker_total_us");
    if (!execution.phases.parameter_build_us || !execution.phases.host_to_device_us ||
        !execution.phases.witness_selection_us || !execution.phases.a_pow_k_gpu_us ||
        !execution.phases.main_ntt_gpu_us || !execution.phases.gerbicz_gpu_us ||
        !execution.phases.final_reduce_gpu_us || !execution.phases.device_to_host_us ||
        !execution.phases.worker_total_us) {
        throw std::runtime_error("native batch timing marker is incomplete");
    }
    execution.result_parse_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - parse_start).count());
    return execution;
}

CampaignSummary run_campaign(
    const CampaignConfig& config,
    const BatchExecutor& executor,
    const Sha256Provider& sha256,
    StopRequested stop_requested) {
    if (config.campaign_directory.empty() || !std::filesystem::is_directory(config.campaign_directory) ||
        config.campaign_id.empty() || config.parent_campaign_id.empty() || config.engine_commit.empty() ||
        config.batch_size != 8U || !executor) {
        throw std::invalid_argument("invalid native B8 campaign configuration");
    }
    require_sha256(config.engine_binary_sha256, "engine_binary_sha256");
    require_sha256(config.survivor_list_sha256, "survivor_list_sha256");
    if (!stop_requested) { stop_requested = [] { return false; }; }

    const auto queue = read_candidate_file(config.remaining_queue_path);
    const auto parent_completed = read_candidate_file(config.parent_completed_path);
    std::set<std::pair<std::uint32_t, std::uint32_t>> corpus;
    for (const auto& candidate : parent_completed) { corpus.emplace(candidate.k, candidate.n); }
    for (const auto& candidate : queue) {
        if (!corpus.emplace(candidate.k, candidate.n).second) {
            throw std::runtime_error("parent completed set overlaps the remaining queue");
        }
    }

    const auto results_path = config.campaign_directory / "candidate-results.tsv";
    const auto checkpoint_path = config.campaign_directory / "campaign.checkpoint.json";
    const auto telemetry_path = config.campaign_directory / "campaign-telemetry.tsv";
    const auto status_path = config.campaign_directory / "status.json";
    std::filesystem::create_directories(config.campaign_directory / "logs");
    std::filesystem::create_directories(config.campaign_directory / "work");

    auto stored_results = read_results(results_path, config, sha256);
    auto results_content = serialize_results(stored_results);
    const auto result_state = validate_result_batches(stored_results, sha256);
    if (stored_results.size() > queue.size()) {
        throw std::runtime_error("durable results exceed the immutable remaining queue");
    }
    for (std::size_t index = 0U; index < stored_results.size(); ++index) {
        if (stored_results[index].result.candidate != queue[index]) {
            throw std::runtime_error("durable results are not an exact prefix of the immutable remaining queue");
        }
    }
    validate_checkpoint(
        checkpoint_path, config, stored_results, result_state, parent_completed, queue, sha256);
    std::uint64_t next_batch_id = result_state.next_batch_id;
    std::vector<Candidate> remaining(
        queue.begin() + static_cast<std::ptrdiff_t>(stored_results.size()), queue.end());

    TelemetryWriter telemetry(telemetry_path, config.campaign_id, config.telemetry_limits);
    double accumulated_gpu_energy_wh{};
    if (const auto previous = last_complete_tsv_value(telemetry_path, "gpu_energy_wh_integrated")) {
        accumulated_gpu_energy_wh = finite_number(*previous).value_or(0.0);
    }
    const auto run_start = Clock::now();
    const auto initial_completed = stored_results.size();
    RuntimeProcessIds runtime_ids;
    std::string last_gpu_temperature{"UNKNOWN"};
    std::string last_gpu_power{"UNKNOWN"};
    std::string last_checkpoint_utc = std::filesystem::exists(checkpoint_path)
        ? require_json_string(read_file(checkpoint_path), "updated_utc") : "UNKNOWN";
    std::uint64_t checkpoint_bytes_written{};
    std::size_t error_count{};
    std::string last_error;
    bool prime_found = std::ranges::any_of(stored_results, [](const StoredResult& result) {
        return result.result.classification == "PROVEN_PRIME";
    });
    std::string state{prime_found ? "PRIME_FOUND" : "IN_PROGRESS"};

    const auto throughput = [&] {
        const auto seconds = std::chrono::duration<double>(Clock::now() - run_start).count();
        const auto completed = stored_results.size() - initial_completed;
        return completed == 0U || seconds <= 0.0 ? 0.0 : static_cast<double>(completed) * 3600.0 / seconds;
    };
    const auto write_status = [&](const std::optional<std::uint64_t> active_batch = std::nullopt) {
        std::ostringstream speed;
        speed << std::fixed << std::setprecision(6) << throughput();
        const auto completed_resume = stored_results.size();
        const auto status = "{\"active_batch_id\":" +
            (active_batch ? json_escape(std::to_string(*active_batch)) : "null") +
            ",\"campaign_id\":" + json_escape(config.campaign_id) +
            ",\"completed_this_resume\":" + json_escape(std::to_string(completed_resume)) +
            ",\"completed_total\":" + json_escape(std::to_string(parent_completed.size() + completed_resume)) +
            ",\"error_count\":" + json_escape(std::to_string(error_count)) +
            ",\"eta_utc\":" + json_escape(eta_utc(remaining.size(), throughput())) +
            ",\"gpu_power_w\":" + json_escape(last_gpu_power) +
            ",\"gpu_temperature_c\":" + json_escape(last_gpu_temperature) +
            ",\"last_checkpoint_utc\":" + json_escape(last_checkpoint_utc) +
            ",\"last_error\":" + json_escape(last_error) +
            ",\"prime_found\":" + std::string{prime_found ? "true" : "false"} +
            ",\"remaining\":" + json_escape(std::to_string(remaining.size())) +
            ",\"state\":" + json_escape(state) +
            ",\"supervisor_pid\":" + json_escape(std::to_string(config.supervisor_pid)) +
            ",\"telemetry_bytes\":" + json_escape(std::to_string(telemetry.bytes_written())) +
            ",\"throughput_candidates_per_hour\":" + json_escape(speed.str()) +
            ",\"watchdog_pid\":" + json_escape(std::to_string(runtime_ids.watchdog_pid)) +
            ",\"worker_pid\":" + json_escape(std::to_string(runtime_ids.worker_pid)) + "}\n";
        if (status.size() >= 16U * 1024U) { throw std::runtime_error("status.json exceeds 16 KiB"); }
        work::write_checkpoint_atomically(status_path, status);
    };

    telemetry.append("RUN_START", "PROCESS_GLOBAL", {
        {"completed_unique", std::to_string(parent_completed.size() + stored_results.size())},
        {"remaining", std::to_string(remaining.size())},
        {"engine_hash", config.engine_binary_sha256},
        {"message", "native B=8 production resume"}}, true);
    write_status();

    auto durable_checkpoint = [&](const std::uint64_t last_batch_id, const std::string_view last_batch_hash,
                                  const std::string_view terminal_status) {
        auto all_completed = parent_completed;
        const auto resume_candidates = resume_completed_candidates(stored_results);
        all_completed.insert(all_completed.end(), resume_candidates.begin(), resume_candidates.end());
        last_checkpoint_utc = utc_now();
        const auto content = checkpoint_json(
            config, all_completed, remaining, hash_bytes(results_content, sha256), next_batch_id,
            last_batch_id, last_batch_hash, terminal_status, sha256, last_checkpoint_utc);
        work::write_checkpoint_atomically(checkpoint_path, content);
        checkpoint_bytes_written += content.size();
    };

    std::uint64_t last_batch_id = next_batch_id == 0U ? 0U : next_batch_id - 1U;
    std::string last_batch_hash = stored_results.empty()
        ? std::string(64U, '0') : stored_results.back().batch_result_hash;
    durable_checkpoint(last_batch_id, last_batch_hash, state);
    telemetry.flush(true);
    write_status();

    while (!remaining.empty() && !prime_found) {
        if (stop_requested()) {
            state = "STOPPED";
            durable_checkpoint(last_batch_id, last_batch_hash, state);
            telemetry.append("SAFETY_EVENT", "PROCESS_GLOBAL", {{"terminal_status", state}, {"message", "operator stop before next batch"}}, true);
            telemetry.flush(true);
            break;
        }

        const auto batch_start_clock = Clock::now();
        const auto prepare_start = Clock::now();
        const auto count = std::min<std::size_t>(config.batch_size, remaining.size());
        std::vector<Candidate> candidates(remaining.begin(), remaining.begin() + static_cast<std::ptrdiff_t>(count));
        BatchRequest request;
        request.batch_id = next_batch_id;
        request.candidates = candidates;
        request.ordered_candidate_hash = ordered_batch_sha256(candidates, sha256);
        request.start_utc = utc_now();
        const auto batch_name = "batch-" + std::to_string(request.batch_id);
        while (std::filesystem::exists(
            config.campaign_directory / "logs" /
            (batch_name + "-attempt-" + std::to_string(request.attempt_id) + ".stdout.log"))) {
            ++request.attempt_id;
        }
        const auto name = batch_name + "-attempt-" + std::to_string(request.attempt_id);
        request.input_path = config.campaign_directory / "work" / (name + ".pending.txt");
        request.stdout_path = config.campaign_directory / "logs" / (name + ".stdout.log");
        request.stderr_path = config.campaign_directory / "logs" / (name + ".stderr.log");
        std::string input_text;
        for (const auto& candidate : candidates) {
            input_text += std::to_string(candidate.k) + " " + std::to_string(candidate.n) + "\n";
        }
        work::write_checkpoint_atomically(request.input_path, input_text);
        const auto scheduler_prepare_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - prepare_start).count());
        telemetry.append("BATCH_START", "BATCH_SHARED", {
            {"batch_id", std::to_string(request.batch_id)}, {"batch_size", std::to_string(count)},
            {"ordered_candidate_hash", request.ordered_candidate_hash}, {"engine_hash", config.engine_binary_sha256}}, true);
        state = "IN_PROGRESS";
        write_status(request.batch_id);

        double batch_gpu_energy_wh{};
        const ResourceSink resource_sink = [&](const TelemetryFields& sample) {
            TelemetryFields fields = sample;
            fields["active_batch_id"] = std::to_string(request.batch_id);
            fields["active_batch_size"] = std::to_string(count);
            fields["completed_unique"] = std::to_string(parent_completed.size() + stored_results.size());
            fields["remaining"] = std::to_string(remaining.size());
            fields["survivor_queue_bytes"] = std::to_string(std::filesystem::file_size(config.remaining_queue_path));
            fields["telemetry_buffer_bytes"] = std::to_string(telemetry.buffered_bytes());
            fields["checkpoint_bytes_written"] = std::to_string(checkpoint_bytes_written);
            fields["telemetry_bytes_written"] = std::to_string(telemetry.bytes_written());
            if (const auto found = fields.find("gpu_energy_wh_integrated"); found != fields.end()) {
                if (const auto measured = finite_number(found->second)) {
                    batch_gpu_energy_wh = *measured;
                    found->second = decimal_number(accumulated_gpu_energy_wh + batch_gpu_energy_wh);
                }
            }
            if (const auto found = fields.find("gpu_temperature_c"); found != fields.end()) {
                last_gpu_temperature = found->second;
            }
            if (const auto found = fields.find("gpu_power_w"); found != fields.end()) {
                last_gpu_power = found->second;
            }
            if (const auto reason = fields.find("reason"); reason != fields.end()) {
                fields["message"] = reason->second;
            }
            TelemetryFields process_fields;
            for (const auto* name : {"process_cpu_percent", "process_cpu_time_ms", "scheduler_thread_cpu_ms",
                                     "process_working_set_bytes", "process_private_bytes", "process_read_bytes",
                                     "process_write_bytes"}) {
                if (const auto found = fields.find(name); found != fields.end()) {
                    process_fields.emplace(found->first, found->second);
                    fields.erase(found);
                }
            }
            process_fields["active_batch_id"] = std::to_string(request.batch_id);
            process_fields["active_batch_size"] = std::to_string(count);
            process_fields["message"] = "process metrics describe the active Proth20 GPU worker";
            if (telemetry.append("RESOURCE_SAMPLE", "SYSTEM_GLOBAL", fields)) {
                static_cast<void>(telemetry.append("RESOURCE_SAMPLE", "PROCESS_GLOBAL", process_fields));
            }
            write_status(request.batch_id);
        };
        const RuntimeIdsSink runtime_sink = [&](const RuntimeProcessIds& ids) {
            runtime_ids = ids;
            write_status(request.batch_id);
        };

        BatchExecution execution;
        try {
            execution = executor(request, resource_sink, runtime_sink, stop_requested);
        } catch (const std::exception& error) {
            ++error_count;
            last_error = error.what();
            state = "ERROR";
            telemetry.append("ERROR", "PROCESS_GLOBAL", {
                {"batch_id", std::to_string(request.batch_id)}, {"error_code", "BATCH_EXECUTION_FAILED"},
                {"message", last_error}}, true);
            durable_checkpoint(last_batch_id, last_batch_hash, state);
            write_status(request.batch_id);
            telemetry.append("RUN_END", "PROCESS_GLOBAL", {{"terminal_status", state}, {"message", last_error}}, true);
            telemetry.flush(true);
            throw;
        }
        accumulated_gpu_energy_wh += batch_gpu_energy_wh;
        const auto result_parse_us = execution.result_parse_us;
        runtime_ids = {};
        if (execution.stopped && !execution.error.empty()) {
            ++error_count;
            last_error = execution.error;
            state = "ERROR";
            telemetry.append("ERROR", "PROCESS_GLOBAL", {
                {"batch_id", std::to_string(request.batch_id)}, {"error_code", "WATCHDOG_STOP"},
                {"message", last_error}}, true);
            durable_checkpoint(last_batch_id, last_batch_hash, state);
            write_status();
            telemetry.flush(true);
            throw std::runtime_error(execution.error);
        }
        if (execution.stopped) {
            state = "STOPPED";
            telemetry.append("SAFETY_EVENT", "PROCESS_GLOBAL", {
                {"batch_id", std::to_string(request.batch_id)}, {"terminal_status", state},
                {"message", "native worker stopped before durable batch completion"}}, true);
            durable_checkpoint(last_batch_id, last_batch_hash, state);
            write_status();
            telemetry.flush(true);
            break;
        }
        if (execution.worker_exit_code != 0 || !execution.error.empty() || execution.results.size() != candidates.size()) {
            throw std::runtime_error("native worker returned an invalid complete batch");
        }
        for (std::size_t lane = 0U; lane < execution.results.size(); ++lane) {
            auto& result = execution.results[lane];
            if (result.lane_id != lane || result.candidate != candidates[lane]) {
                throw std::runtime_error("native worker result order differs from ordered batch");
            }
            result.candidate_result_hash = hash_bytes(
                result_hash_input(config.campaign_id, request.batch_id, result), sha256);
        }
        const auto current_batch_hash = batch_hash(
            request, execution.results, config.engine_binary_sha256, sha256);
        const bool batch_has_prime = std::ranges::any_of(execution.results, [](const CandidateResult& result) {
            return result.classification == "PROVEN_PRIME";
        });

        const auto persist_start = Clock::now();
        for (const auto& result : execution.results) {
            stored_results.push_back({
                config.campaign_id, request.batch_id, request.ordered_candidate_hash, count, result,
                config.engine_binary_sha256, request.start_utc, "BATCH_COMPLETE", current_batch_hash});
        }
        results_content = serialize_results(stored_results);
        work::write_checkpoint_atomically(results_path, results_content);
        const auto result_persist_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - persist_start).count());
        remaining.erase(remaining.begin(), remaining.begin() + static_cast<std::ptrdiff_t>(count));
        ++next_batch_id;
        last_batch_id = request.batch_id;
        last_batch_hash = current_batch_hash;
        prime_found = prime_found || batch_has_prime;
        state = prime_found ? "PRIME_FOUND" : (remaining.empty() ? "COMPLETE_NO_PRIME" : "IN_PROGRESS");

        for (const auto& result : execution.results) {
            telemetry.append("CANDIDATE_RESULT", "CANDIDATE_EXACT", {
                {"batch_id", std::to_string(request.batch_id)}, {"batch_size", std::to_string(count)},
                {"lane_id", std::to_string(result.lane_id)}, {"k", std::to_string(result.candidate.k)},
                {"n", std::to_string(result.candidate.n)}, {"classification", result.classification},
                {"witness", std::to_string(result.witness)}, {"RES64", result.res64},
                {"validation_status", result.validation_status}, {"candidate_result_hash", result.candidate_result_hash},
                {"ordered_candidate_hash", request.ordered_candidate_hash}, {"engine_hash", config.engine_binary_sha256},
                {"terminal_status", "BATCH_COMPLETE"}, {"result_hash", current_batch_hash}}, true);
        }
        const auto checkpoint_start = Clock::now();
        durable_checkpoint(last_batch_id, last_batch_hash, state);
        const auto checkpoint_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - checkpoint_start).count());
        const auto batch_wall_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - batch_start_clock).count());
        telemetry.append("BATCH_PHASES", "BATCH_SHARED", {
            {"batch_id", std::to_string(request.batch_id)}, {"batch_size", std::to_string(count)},
            {"scheduler_prepare_us", std::to_string(scheduler_prepare_us)},
            {"parameter_build_us", optional_decimal(execution.phases.parameter_build_us)},
            {"host_to_device_us", optional_decimal(execution.phases.host_to_device_us)},
            {"witness_selection_us", optional_decimal(execution.phases.witness_selection_us)},
            {"a_pow_k_gpu_us", optional_decimal(execution.phases.a_pow_k_gpu_us)},
            {"main_ntt_gpu_us", optional_decimal(execution.phases.main_ntt_gpu_us)},
            {"gerbicz_gpu_us", optional_decimal(execution.phases.gerbicz_gpu_us)},
            {"final_reduce_gpu_us", optional_decimal(execution.phases.final_reduce_gpu_us)},
            {"device_to_host_us", optional_decimal(execution.phases.device_to_host_us)},
            {"result_parse_us", std::to_string(result_parse_us)}, {"result_persist_us", std::to_string(result_persist_us)},
            {"checkpoint_us", std::to_string(checkpoint_us)},
            {"worker_total_us", optional_decimal(execution.phases.worker_total_us)},
            {"batch_wall_us", std::to_string(batch_wall_us)}}, true);
        telemetry.append("BATCH_PHASES", "DERIVED_AVERAGE", {
            {"batch_id", std::to_string(request.batch_id)}, {"batch_size", std::to_string(count)},
            {"derived_average_us", std::to_string(batch_wall_us / count)},
            {"message", "batch_wall_us / batch_size; not a physical per-candidate GPU measurement"}}, true);

        telemetry.append("CHECKPOINT", "PROCESS_GLOBAL", {
            {"batch_id", std::to_string(request.batch_id)}, {"checkpoint_us", std::to_string(checkpoint_us)},
            {"completed_unique", std::to_string(parent_completed.size() + stored_results.size())},
            {"remaining", std::to_string(remaining.size())}, {"terminal_status", state},
            {"result_hash", hash_bytes(results_content, sha256)}}, true);
        telemetry.append("BATCH_END", "BATCH_SHARED", {
            {"batch_id", std::to_string(request.batch_id)}, {"batch_size", std::to_string(count)},
            {"terminal_status", state}, {"result_hash", current_batch_hash},
            {"batch_wall_us", std::to_string(batch_wall_us)}}, true);
        telemetry.flush(true);
        write_status();
        if (prime_found) { break; }
    }

    if (state == "IN_PROGRESS" && remaining.empty()) { state = "COMPLETE_NO_PRIME"; }
    telemetry.append("RUN_END", "PROCESS_GLOBAL", {
        {"terminal_status", state}, {"completed_unique", std::to_string(parent_completed.size() + stored_results.size())},
        {"remaining", std::to_string(remaining.size())}}, true);
    telemetry.flush(true);
    write_status();
    return {
        state, parent_completed.size(), stored_results.size(), remaining.size(), next_batch_id,
        prime_found, throughput(), hash_bytes(results_content, sha256)};
}

}  // namespace primeforge::discovery::native_b8
