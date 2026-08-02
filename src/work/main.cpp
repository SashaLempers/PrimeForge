// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/work/work_unit.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::uint64_t parse_u64(
    const std::string_view text, const std::string_view argument) {
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid unsigned integer for " + std::string{argument});
    }
    return value;
}

int run(const int argc, char** argv) {
    std::uint64_t begin = 0U;
    std::uint64_t end = 0U;
    std::uint64_t span = 0U;
    std::filesystem::path output_directory;
    primeforge::work::WorkUnit prototype;
    prototype.residue_compiler_version = "primeforge-residue-v0";
    prototype.sieve_bounds = {2U, 1'000'000U};
    prototype.proof_policy = "NONE";

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto next = [&]() -> std::string_view {
            if (++index >= argc) {
                throw std::invalid_argument("missing value after " + std::string{argument});
            }
            return argv[index];
        };
        if (argument == "--begin") {
            begin = parse_u64(next(), argument);
        } else if (argument == "--end") {
            end = parse_u64(next(), argument);
        } else if (argument == "--span") {
            span = parse_u64(next(), argument);
        } else if (argument == "--output-dir") {
            output_directory = next();
        } else if (argument == "--family-id") {
            prototype.family_id = next();
        } else if (argument == "--definition-sha256") {
            prototype.canonical_definition_sha256 = next();
        } else if (argument == "--constraint") {
            prototype.constraints.emplace_back(next());
        } else if (argument == "--residue-compiler-version") {
            prototype.residue_compiler_version = next();
        } else if (argument == "--sieve-min") {
            prototype.sieve_bounds.minimum_prime = parse_u64(next(), argument);
        } else if (argument == "--sieve-max") {
            prototype.sieve_bounds.maximum_prime = parse_u64(next(), argument);
        } else if (argument == "--proof-policy") {
            prototype.proof_policy = next();
        } else if (argument == "--seed") {
            prototype.seed = parse_u64(next(), argument);
        } else {
            throw std::invalid_argument("unknown argument: " + std::string{argument});
        }
    }
    if (span == 0U || output_directory.empty() || prototype.family_id.empty() ||
        prototype.canonical_definition_sha256.empty()) {
        throw std::invalid_argument(
            "--span, --output-dir, --family-id, and --definition-sha256 are required");
    }
    if (begin >= end) {
        throw std::invalid_argument("CLI work-unit interval must be nonempty and half-open");
    }

    const primeforge::PortableSha256Provider provider;
    const auto units = primeforge::work::partition_work_units(
        prototype, begin, end, span, provider);
    const auto verification = primeforge::work::verify_coverage(begin, end, units, provider);
    if (!verification.valid) {
        throw std::runtime_error("generated work-unit partition failed its coverage check");
    }
    std::string json_lines;
    for (const auto& unit : units) {
        json_lines += primeforge::work::canonical_work_unit(unit);
        json_lines.push_back('\n');
    }
    std::filesystem::create_directories(output_directory);
    primeforge::work::write_checkpoint_atomically(
        output_directory / "work_units.jsonl", json_lines);
    primeforge::work::write_checkpoint_atomically(
        output_directory / "coverage_report.json", verification.canonical_report_json);

    std::cout << "work_unit_count=" << units.size() << '\n'
              << "coverage=PASS\n"
              << "interval=[" << begin << ',' << end << ")\n"
              << "output_directory=" << output_directory.string() << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "primeforge-work-units: FAIL: " << error.what() << '\n';
        return 1;
    }
}
