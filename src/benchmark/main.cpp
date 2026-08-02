// SPDX-License-Identifier: Apache-2.0

#include "primeforge/benchmark/benchmark.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::filesystem::path output_directory;
    std::filesystem::path candidates_path;
    std::string run_id;
    std::string commit;
    std::string candidate_set_sha256;
    std::string environment_metadata_file;
    std::size_t repetitions{9};
    std::uint64_t seed{20260802};
};

struct Measurement {
    std::string run_id;
    std::string variant;
    std::string candidate_set_sha256;
    std::size_t candidate_count{};
    std::size_t repetition{};
    std::size_t order_index{};
    std::uint64_t checksum{};
    std::uint64_t transfer_ns{};
    std::uint64_t kernel_ns{};
    std::uint64_t proof_ns{};
    std::uint64_t io_ns{};
    std::uint64_t total_ns{};
    std::string telemetry_assessment;
    std::string invalid_reason;
};

[[nodiscard]] std::uint64_t parse_u64(const std::string_view value, const std::string_view name) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::invalid_argument("invalid " + std::string{name});
    }
    return result;
}

[[nodiscard]] Options parse_options(const int argc, char** argv) {
    std::map<std::string, std::string> values;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc || std::string_view{argv[index]}.substr(0, 2) != "--") {
            throw std::invalid_argument("options must be --name value pairs");
        }
        values[std::string{argv[index]}.substr(2)] = argv[index + 1];
    }

    const auto required = [&values](const std::string& name) -> const std::string& {
        const auto found = values.find(name);
        if (found == values.end() || found->second.empty()) {
            throw std::invalid_argument("missing --" + name);
        }
        return found->second;
    };

    Options options{};
    options.output_directory = required("output-dir");
    options.candidates_path = required("candidates");
    options.run_id = required("run-id");
    options.commit = required("commit");
    options.candidate_set_sha256 = required("candidate-set-sha256");
    options.environment_metadata_file = required("environment-metadata-file");
    if (values.contains("repetitions")) {
        options.repetitions = static_cast<std::size_t>(parse_u64(values.at("repetitions"), "repetitions"));
    }
    if (values.contains("seed")) {
        options.seed = parse_u64(values.at("seed"), "seed");
    }
    if (options.repetitions < 7U) {
        throw std::invalid_argument("at least seven repetitions are mandatory");
    }
    if (options.candidate_set_sha256.size() != 64U) {
        throw std::invalid_argument("candidate-set-sha256 must contain 64 hexadecimal characters");
    }
    for (const char character : options.run_id) {
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '-' || character == '_')) {
            throw std::invalid_argument("run-id contains a non-portable character");
        }
    }
    return options;
}

[[nodiscard]] std::vector<std::uint64_t> load_candidates(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string line;
    if (!input || !std::getline(input, line)) {
        throw std::runtime_error("cannot read candidate corpus");
    }
    std::vector<std::uint64_t> candidates;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::size_t field = 0;
        std::size_t start = 0;
        std::string_view decimal;
        while (start <= line.size()) {
            const auto separator = line.find('\t', start);
            if (field == 3U) {
                const auto length = separator == std::string::npos ? line.size() - start : separator - start;
                decimal = std::string_view{line}.substr(start, length);
                break;
            }
            if (separator == std::string::npos) {
                break;
            }
            start = separator + 1U;
            ++field;
        }
        if (decimal.empty()) {
            throw std::runtime_error("candidate row has no decimal field");
        }
        candidates.push_back(parse_u64(decimal, "candidate decimal"));
    }
    if (candidates.empty()) {
        throw std::runtime_error("candidate corpus is empty");
    }
    return candidates;
}

[[nodiscard]] std::uint64_t workload(const std::span<const std::uint64_t> candidates) noexcept {
    std::uint64_t checksum = 0xcbf29ce484222325ULL;
    for (std::size_t round = 0; round < 20'000U; ++round) {
        for (const auto candidate : candidates) {
            checksum ^= candidate + static_cast<std::uint64_t>(round);
            checksum *= 0x100000001b3ULL;
            checksum ^= checksum >> 29U;
        }
    }
    return checksum;
}

template <typename Callable>
[[nodiscard]] std::pair<std::uint64_t, std::invoke_result_t<Callable>> timed(Callable&& callable) {
    const auto start = Clock::now();
    auto value = callable();
    const auto end = Clock::now();
    const auto count = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return {static_cast<std::uint64_t>(count), std::move(value)};
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::string output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (character < 0x20U) {
                    constexpr char digits[] = "0123456789abcdef";
                    output += "\\u00";
                    output += digits[(character >> 4U) & 0x0fU];
                    output += digits[character & 0x0fU];
                } else {
                    output += static_cast<char>(character);
                }
        }
    }
    return output;
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create output file: " + path.string());
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("cannot finish output file: " + path.string());
    }
}

[[nodiscard]] std::string render_measurement_csv(const Measurement& item) {
    std::ostringstream row;
    row << item.run_id << ',' << item.repetition << ',' << item.order_index << ',' << item.variant << ','
        << item.candidate_set_sha256 << ',' << item.candidate_count << ',' << item.checksum << ','
        << item.transfer_ns << ',' << item.kernel_ns << ',' << item.proof_ns << ',' << item.io_ns << ','
        << item.total_ns << ',' << item.telemetry_assessment
        << ",UNKNOWN,UNKNOWN,UNKNOWN,UNKNOWN,UNKNOWN,UNKNOWN,NO," << item.invalid_reason << '\n';
    return row.str();
}

[[nodiscard]] std::string render_measurement_json(const Measurement& item) {
    std::ostringstream json;
    json << "{\"candidate_count\":" << item.candidate_count
         << ",\"candidate_set_sha256\":\"" << item.candidate_set_sha256
         << "\",\"checksum\":\"" << item.checksum
         << "\",\"cpu_frequency_hz\":\"UNKNOWN\",\"cpu_temperature_millicelsius\":\"UNKNOWN\""
         << ",\"gpu_frequency_hz\":\"UNKNOWN\",\"gpu_temperature_millicelsius\":\"UNKNOWN\""
         << ",\"invalid_reason\":\"" << item.invalid_reason
         << "\",\"io_ns\":" << item.io_ns << ",\"kernel_ns\":" << item.kernel_ns
         << ",\"order_index\":" << item.order_index << ",\"power_milliwatts\":\"UNKNOWN\""
         << ",\"proof_ns\":" << item.proof_ns << ",\"repetition\":" << item.repetition
         << ",\"run_id\":\"" << item.run_id << "\",\"telemetry_assessment\":\""
         << item.telemetry_assessment << "\",\"total_ns\":" << item.total_ns
         << ",\"transfer_ns\":" << item.transfer_ns << ",\"valid_for_performance\":\"NO\""
         << ",\"variant\":\"" << item.variant
         << "\",\"wall_energy_millijoules\":\"UNKNOWN\"}";
    return json.str();
}

int run(const Options& options) {
    std::filesystem::create_directories(options.output_directory);
    const auto candidates = load_candidates(options.candidates_path);
    constexpr std::array<std::string_view, 2> variants{"reference-a", "reference-b"};
    const auto expected_checksum = workload(candidates);

    // Startup, parsing, allocation, and these warmups occur before stable samples.
    for (std::size_t warmup = 0; warmup < 3U; ++warmup) {
        for (const auto variant : variants) {
            static_cast<void>(variant);
            if (workload(candidates) != expected_checksum) {
                throw std::runtime_error("warmup checksum divergence");
            }
        }
    }

    const auto schedule = primeforge::benchmark::randomized_variant_schedule(
        variants.size(), options.repetitions, options.seed);
    std::vector<Measurement> measurements;
    measurements.reserve(schedule.size());

    for (std::size_t order_index = 0; order_index < schedule.size(); ++order_index) {
        const auto variant_index = schedule[order_index];
        const auto repetition = order_index / variants.size();
        const auto [transfer_ns, working_candidates] = timed([&candidates] { return candidates; });
        const auto [kernel_ns, checksum] = timed([&working_candidates] { return workload(working_candidates); });
        const auto [proof_ns, proof_valid] = timed([checksum, expected_checksum] { return checksum == expected_checksum; });
        if (!proof_valid) {
            throw std::runtime_error("measured checksum divergence");
        }

        Measurement item{
            options.run_id,
            std::string{variants[variant_index]},
            options.candidate_set_sha256,
            candidates.size(),
            repetition,
            order_index,
            checksum,
            transfer_ns,
            kernel_ns,
            proof_ns,
            0,
            0,
            "UNAVAILABLE",
            "TELEMETRY_UNAVAILABLE",
        };
        const auto [io_ns, rendered] = timed([&item] { return render_measurement_csv(item); });
        static_cast<void>(rendered);
        item.io_ns = io_ns;
        item.total_ns = item.transfer_ns + item.kernel_ns + item.proof_ns + item.io_ns;
        measurements.push_back(std::move(item));
    }

    std::ostringstream raw_csv;
    raw_csv << "run_id,repetition,order_index,variant,candidate_set_sha256,candidate_count,checksum,transfer_ns,"
               "kernel_ns,proof_ns,io_ns,total_ns,telemetry_assessment,cpu_temperature_millicelsius,"
               "cpu_frequency_hz,gpu_temperature_millicelsius,gpu_frequency_hz,power_milliwatts,"
               "wall_energy_millijoules,valid_for_performance,invalid_reason\n";
    for (const auto& item : measurements) {
        raw_csv << render_measurement_csv(item);
    }

    std::ostringstream raw_json;
    raw_json << "{\"measurements\":[";
    for (std::size_t index = 0; index < measurements.size(); ++index) {
        if (index != 0U) {
            raw_json << ',';
        }
        raw_json << render_measurement_json(measurements[index]);
    }
    raw_json << "],\"run_id\":\"" << options.run_id << "\",\"schema_version\":1}";

    std::ostringstream summary_csv;
    summary_csv << "run_id,variant,repetitions,min_ns,max_ns,median_ns,mad_ns,confidence_low_ns,confidence_high_ns,"
                   "telemetry_status,performance_claim\n";
    std::ostringstream statistics_json;
    statistics_json << '[';
    for (std::size_t variant_index = 0; variant_index < variants.size(); ++variant_index) {
        std::vector<std::uint64_t> totals;
        for (const auto& item : measurements) {
            if (item.variant == variants[variant_index]) {
                totals.push_back(item.total_ns);
            }
        }
        const auto statistics = primeforge::benchmark::summarize(totals);
        summary_csv << options.run_id << ',' << variants[variant_index] << ',' << totals.size() << ','
                    << statistics.minimum << ',' << statistics.maximum << ',' << statistics.median << ','
                    << statistics.median_absolute_deviation << ',' << statistics.confidence_low << ','
                    << statistics.confidence_high << ",UNAVAILABLE,NONE\n";
        if (variant_index != 0U) {
            statistics_json << ',';
        }
        statistics_json << "{\"confidence_high_ns\":" << statistics.confidence_high
                        << ",\"confidence_low_ns\":" << statistics.confidence_low
                        << ",\"mad_ns\":" << statistics.median_absolute_deviation
                        << ",\"max_ns\":" << statistics.maximum << ",\"median_ns\":" << statistics.median
                        << ",\"min_ns\":" << statistics.minimum << ",\"repetitions\":" << totals.size()
                        << ",\"variant\":\"" << variants[variant_index] << "\"}";
    }
    statistics_json << ']';

    std::ostringstream summary_json;
    summary_json << "{\"candidate_set_sha256\":\"" << options.candidate_set_sha256
                 << "\",\"commit\":\"" << json_escape(options.commit)
                 << "\",\"environment_metadata_file\":\"" << json_escape(options.environment_metadata_file)
                 << "\",\"performance_claim\":\"NONE\",\"run_id\":\"" << options.run_id
                 << "\",\"schema_version\":1,\"statistics\":" << statistics_json.str()
                 << ",\"telemetry_status\":\"UNAVAILABLE\",\"warmup_iterations\":3}";

    const auto prefix = options.output_directory / options.run_id;
    write_text(prefix.string() + "-raw.csv", raw_csv.str());
    write_text(prefix.string() + "-raw.json", raw_json.str());
    write_text(prefix.string() + "-summary.csv", summary_csv.str());
    write_text(prefix.string() + "-summary.json", summary_json.str());

    std::cout << "PrimeForge benchmark harness PASS: " << measurements.size()
              << " samples; telemetry UNKNOWN; performance claims disabled\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge benchmark harness FAIL: " << error.what() << '\n';
        return 1;
    }
}
