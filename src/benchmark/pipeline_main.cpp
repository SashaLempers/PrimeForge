// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/prp/base2_batch.hpp"

#if defined(PRIMEFORGE_HAS_CUDA_PRP)
#include "primeforge/cuda/prp_batch.hpp"
#endif

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef PRIMEFORGE_BUILD_COMMIT
#define PRIMEFORGE_BUILD_COMMIT "UNKNOWN"
#endif

#ifndef PRIMEFORGE_CUDA_VERSION
#define PRIMEFORGE_CUDA_VERSION "UNKNOWN"
#endif

namespace {

struct Profile {
    std::string schema;
    std::string profile_id;
    std::string family;
    std::string input_kind;
    std::uint64_t bit_width{};
    std::uint64_t candidate_count{};
    std::uint64_t seed{};
    bool implemented{};
};

struct Options {
    std::filesystem::path profile;
    std::filesystem::path output;
    std::string backend;
    std::size_t warmup{3U};
    std::size_t repetitions{7U};
    std::size_t batch_size{};
};

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read file: " + path.string());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_file(const std::filesystem::path& path, const std::string_view content) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("cannot create file: " + path.string());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) throw std::runtime_error("cannot finish file: " + path.string());
}

[[nodiscard]] std::string_view value_after_key(
    const std::string_view json, const std::string_view key) {
    const auto marker = std::string{"\""} + std::string{key} + "\"";
    const auto key_position = json.find(marker);
    if (key_position == std::string_view::npos) {
        throw std::invalid_argument("profile is missing key: " + std::string{key});
    }
    const auto colon = json.find(':', key_position + marker.size());
    if (colon == std::string_view::npos) {
        throw std::invalid_argument("profile key lacks value: " + std::string{key});
    }
    auto begin = colon + 1U;
    while (begin < json.size() && (json[begin] == ' ' || json[begin] == '\t' ||
                                   json[begin] == '\r' || json[begin] == '\n')) {
        ++begin;
    }
    return json.substr(begin);
}

[[nodiscard]] std::string profile_string(
    const std::string_view json, const std::string_view key) {
    const auto tail = value_after_key(json, key);
    if (tail.empty() || tail.front() != '"') {
        throw std::invalid_argument("profile key must be a string: " + std::string{key});
    }
    const auto end = tail.find('"', 1U);
    if (end == std::string_view::npos) {
        throw std::invalid_argument("unterminated profile string: " + std::string{key});
    }
    const auto value = tail.substr(1U, end - 1U);
    if (value.empty() || value.find('\\') != std::string_view::npos) {
        throw std::invalid_argument("profile strings must be simple nonempty UTF-8 values");
    }
    return std::string{value};
}

[[nodiscard]] std::uint64_t parse_u64(
    const std::string_view value, const std::string_view name) {
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::invalid_argument("invalid " + std::string{name});
    }
    return result;
}

[[nodiscard]] std::uint64_t profile_u64(
    const std::string_view json, const std::string_view key) {
    auto tail = value_after_key(json, key);
    const bool quoted = !tail.empty() && tail.front() == '"';
    if (quoted) tail.remove_prefix(1U);
    std::size_t length = 0U;
    while (length < tail.size() && tail[length] >= '0' && tail[length] <= '9') ++length;
    if (length == 0U || (quoted && (length >= tail.size() || tail[length] != '"'))) {
        throw std::invalid_argument("profile key must be an unsigned decimal: " +
                                    std::string{key});
    }
    return parse_u64(tail.substr(0U, length), key);
}

[[nodiscard]] bool profile_bool(const std::string_view json, const std::string_view key) {
    const auto tail = value_after_key(json, key);
    if (tail.starts_with("true")) return true;
    if (tail.starts_with("false")) return false;
    throw std::invalid_argument("profile key must be boolean: " + std::string{key});
}

[[nodiscard]] Profile load_profile(const std::filesystem::path& path) {
    const auto json = read_file(path);
    if (json.starts_with("\xef\xbb\xbf")) {
        throw std::invalid_argument("benchmark profile must be UTF-8 without BOM");
    }
    Profile result;
    result.schema = profile_string(json, "schema");
    result.profile_id = profile_string(json, "profile_id");
    result.family = profile_string(json, "family");
    result.input_kind = profile_string(json, "input_kind");
    result.bit_width = profile_u64(json, "bit_width");
    result.candidate_count = profile_u64(json, "candidate_count");
    result.seed = profile_u64(json, "seed");
    result.implemented = profile_bool(json, "implemented");
    if (result.schema != "primeforge.benchmark.profile.v1" ||
        result.candidate_count == 0U ||
        (result.implemented && result.candidate_count > 1'048'576U)) {
        throw std::invalid_argument("unsupported benchmark profile contract");
    }
    return result;
}

[[nodiscard]] Options parse_options(const int argc, char** argv) {
    if (argc < 2 || std::string_view{argv[1]} != "run") {
        throw std::invalid_argument(
            "usage: primeforge-bench run --profile FILE --backend cpu|cuda|auto "
            "--output DIR [--warmup N] [--repetitions N] [--batch-size N]");
    }
    std::map<std::string, std::string> values;
    for (int index = 2; index < argc; index += 2) {
        if (index + 1 >= argc || !std::string_view{argv[index]}.starts_with("--")) {
            throw std::invalid_argument("benchmark options must be --name value pairs");
        }
        values.emplace(std::string{argv[index] + 2}, argv[index + 1]);
    }
    const auto required = [&](const std::string& key) -> const std::string& {
        const auto found = values.find(key);
        if (found == values.end() || found->second.empty()) {
            throw std::invalid_argument("missing --" + key);
        }
        return found->second;
    };
    Options result;
    result.profile = required("profile");
    result.backend = required("backend");
    result.output = required("output");
    if (values.contains("warmup")) {
        result.warmup = static_cast<std::size_t>(parse_u64(values.at("warmup"), "warmup"));
    }
    if (values.contains("repetitions")) {
        result.repetitions =
            static_cast<std::size_t>(parse_u64(values.at("repetitions"), "repetitions"));
    }
    if (values.contains("batch-size")) {
        result.batch_size =
            static_cast<std::size_t>(parse_u64(values.at("batch-size"), "batch-size"));
    }
    if (result.repetitions == 0U || result.warmup > 100U ||
        (result.backend != "cpu" && result.backend != "cuda" && result.backend != "auto")) {
        throw std::invalid_argument("invalid benchmark run options");
    }
    return result;
}

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::vector<std::uint64_t> make_candidates(const Profile& profile) {
    if (!profile.implemented || profile.bit_width != 64U ||
        profile.input_kind != "prp_u64_vectors") {
        throw std::runtime_error(
            "profile is versioned but its multiprecision backend is not implemented in commit A");
    }
    std::vector<std::uint64_t> result;
    result.reserve(static_cast<std::size_t>(profile.candidate_count));
    auto state = profile.seed;
    for (std::uint64_t index = 0U; index < profile.candidate_count; ++index) {
        result.push_back(splitmix64(state) | (std::uint64_t{1} << 63U) | 1U);
    }
    return result;
}

[[nodiscard]] std::string sha256_bytes(
    const std::span<const std::byte> bytes, const primeforge::Sha256Provider& sha256) {
    return primeforge::sha256_to_hex(sha256.digest(bytes));
}

[[nodiscard]] std::string sha256_file(
    const std::filesystem::path& path, const primeforge::Sha256Provider& sha256) {
    const auto content = read_file(path);
    return sha256_bytes(std::as_bytes(std::span{content.data(), content.size()}), sha256);
}

[[nodiscard]] std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm calendar{};
#if defined(_WIN32)
    gmtime_s(&calendar, &time);
#else
    gmtime_r(&time, &calendar);
#endif
    std::ostringstream output;
    output << std::put_time(&calendar, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

[[nodiscard]] std::unique_ptr<primeforge::prp::Base2StrongPrpBatchBackend>
make_backend(const std::string_view backend, const std::size_t capacity) {
    if (backend == "cpu") {
        return primeforge::prp::make_cpu_base2_strong_prp_batch_backend(capacity);
    }
#if defined(PRIMEFORGE_HAS_CUDA_PRP)
    if (backend == "cuda") {
        return primeforge::cuda_backend::make_cuda_base2_strong_prp_batch_backend(capacity);
    }
    if (backend == "auto") {
        return primeforge::cuda_backend::make_auto_cuda_base2_strong_prp_batch_backend(
            capacity, std::min<std::size_t>(512U, capacity));
    }
#else
    if (backend == "cuda") {
        throw std::runtime_error("primeforge-bench was built without CUDA");
    }
#endif
    return primeforge::prp::make_cpu_base2_strong_prp_batch_backend(capacity);
}

[[nodiscard]] std::string compiler_id() {
#if defined(_MSC_VER)
    return "MSVC-" + std::to_string(_MSC_FULL_VER);
#elif defined(__GNUC__)
    return "GCC-" + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    return "UNKNOWN";
#endif
}

[[nodiscard]] std::string json_row(
    const Profile& profile, const std::string_view backend_id,
    const std::size_t repetition, const std::string_view profile_sha256,
    const std::string_view dataset_sha256, const std::string_view binary_sha256,
    const std::string_view result_sha256,
    const primeforge::prp::Base2StrongPrpBatchMetrics& metrics) {
    std::ostringstream row;
    row << "{\"backend\":\"" << backend_id << "\",\"batch_size\":"
        << profile.candidate_count << ",\"binary_sha256\":\"" << binary_sha256
        << "\",\"bits\":" << profile.bit_width << ",\"checkpoint_ns\":0"
        << ",\"commit_sha\":\"" << PRIMEFORGE_BUILD_COMMIT
        << "\",\"compiler\":\"" << compiler_id()
        << "\",\"compiler_flags\":\"C++23_RELEASE_TARGET\""
        << ",\"congruence_ns\":0"
        << ",\"cuda_driver\":\"UNKNOWN\",\"cuda_runtime\":\""
        << (metrics.used_accelerator ? PRIMEFORGE_CUDA_VERSION : "NOT_USED")
        << "\",\"d2h_ns\":" << metrics.device_to_host_ns
        << ",\"dataset_sha256\":\"" << dataset_sha256
        << "\",\"generation_ns\":0,\"gpu_power_mean\":\"UNKNOWN\""
        << ",\"gpu_temperature_max\":\"UNKNOWN\",\"gpu_utilization_mean\":\"UNKNOWN\""
        << ",\"h2d_ns\":" << metrics.host_to_device_ns << ",\"io_ns\":0"
        << ",\"kernel_ns\":" << metrics.kernel_ns << ",\"packing_ns\":0"
        << ",\"profile_id\":\"" << profile.profile_id
        << "\",\"profile_sha256\":\"" << profile_sha256
        << "\",\"proof_ns\":0,\"prp_cpu_ns\":" << metrics.cpu_ns
        << ",\"repetition\":" << repetition << ",\"result_sha256\":\""
        << result_sha256 << "\",\"schema\":\"primeforge.benchmark.raw.v1\""
        << ",\"sieve_ns\":0,\"timestamp_utc\":\"" << utc_timestamp()
        << "\",\"total_ns\":" << metrics.total_ns
        << ",\"valid_measurement\":true,\"verification_ns\":0}";
    return row.str();
}

int run(const int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    const auto profile = load_profile(options.profile);
    auto candidates = make_candidates(profile);
    const auto capacity = options.batch_size == 0U
                              ? candidates.size()
                              : std::min(options.batch_size, candidates.size());
    if (capacity != candidates.size()) {
        throw std::invalid_argument("commit A requires batch-size to cover the complete profile");
    }
    auto backend = make_backend(options.backend, capacity);
    std::vector<primeforge::prp::Base2StrongPrpVerdict> verdicts(candidates.size());
    for (std::size_t warmup = 0U; warmup < options.warmup; ++warmup) {
        static_cast<void>(backend->test(candidates, verdicts));
    }

    const primeforge::PortableSha256Provider sha256;
    const auto profile_sha256 = sha256_file(options.profile, sha256);
    const auto dataset_sha256 = sha256_bytes(std::as_bytes(std::span{candidates}), sha256);
    const auto executable = std::filesystem::absolute(argv[0]);
    const auto binary_sha256 = sha256_file(executable, sha256);
    std::filesystem::create_directories(options.output);
    std::ostringstream jsonl;
    std::ostringstream csv;
    csv << "schema,timestamp_utc,commit_sha,binary_sha256,profile_id,profile_sha256,"
           "dataset_sha256,backend,bits,batch_size,repetition,h2d_ns,kernel_ns,d2h_ns,"
           "prp_cpu_ns,total_ns,result_sha256,valid_measurement\n";
    std::string expected_result_sha256;
    for (std::size_t repetition = 0U; repetition < options.repetitions; ++repetition) {
        const auto metrics = backend->test(candidates, verdicts);
        const auto result_sha256 = sha256_bytes(std::as_bytes(std::span{verdicts}), sha256);
        if (expected_result_sha256.empty()) expected_result_sha256 = result_sha256;
        if (result_sha256 != expected_result_sha256) {
            throw std::runtime_error("benchmark verdict hash diverged between repetitions");
        }
        jsonl << json_row(profile, backend->id(), repetition, profile_sha256,
                          dataset_sha256, binary_sha256, result_sha256, metrics)
              << '\n';
        csv << "primeforge.benchmark.raw.v1," << utc_timestamp() << ','
            << PRIMEFORGE_BUILD_COMMIT << ',' << binary_sha256 << ',' << profile.profile_id
            << ',' << profile_sha256 << ',' << dataset_sha256 << ',' << backend->id() << ','
            << profile.bit_width << ',' << candidates.size() << ',' << repetition << ','
            << metrics.host_to_device_ns << ',' << metrics.kernel_ns << ','
            << metrics.device_to_host_ns << ',' << metrics.cpu_ns << ',' << metrics.total_ns
            << ',' << result_sha256 << ",true\n";
    }
    write_file(options.output / "raw.jsonl", jsonl.str());
    write_file(options.output / "raw.csv", csv.str());
    std::cout << "benchmark.profile=" << profile.profile_id << '\n'
              << "benchmark.backend=" << backend->id() << '\n'
              << "benchmark.repetitions=" << options.repetitions << '\n'
              << "benchmark.result_sha256=" << expected_result_sha256 << '\n'
              << "benchmark.output=" << std::filesystem::absolute(options.output).string() << '\n'
              << "benchmark.status=PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "primeforge-bench: FAIL: " << error.what() << '\n';
        return 1;
    }
}
