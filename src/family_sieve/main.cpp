// SPDX-License-Identifier: Apache-2.0

#include "primeforge/benchmark/benchmark.hpp"
#include "primeforge/congruence/compiler.hpp"
#include "primeforge/core/sha256.hpp"
#include "primeforge/core/system_info.hpp"
#include "primeforge/family_sieve/family_sieve.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace benchmark = primeforge::benchmark;
namespace congruence = primeforge::congruence;
namespace fsieve = primeforge::family_sieve;

struct Arguments {
    std::filesystem::path output_directory;
    std::size_t repetitions{7U};
};

struct Regime {
    std::string name;
    std::uint64_t k_count{};
    std::uint64_t n_count{};
};

struct Variant {
    std::string name;
    fsieve::Options options;
};

struct Sample {
    std::string regime;
    std::string variant;
    std::size_t repetition{};
    std::size_t order{};
    std::uint64_t elapsed_nanoseconds{};
    std::uint64_t candidates{};
    std::uint64_t eliminated{};
    std::uint64_t rule_checks{};
    std::uint64_t modular_checks{};
    std::uint64_t exact_checks{};
    std::uint64_t bounded_magnitude_checks{};
    std::uint64_t big_integer_checks{};
    std::string result_sha256;
    bool vector_applied{};
    bool crt_applied{};
    bool huge_pages_applied{};
    bool pinning_applied{};
    unsigned int affinity_workers_requested{};
    unsigned int affinity_workers_applied{};
};

[[nodiscard]] std::uint64_t parse_u64(const std::string_view text) {
    std::size_t consumed = 0U;
    const auto value = std::stoull(std::string{text}, &consumed, 10);
    if (consumed != text.size()) throw std::invalid_argument("invalid unsigned integer");
    return value;
}

[[nodiscard]] Arguments parse_arguments(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (index + 1 >= argc) throw std::invalid_argument("option requires a value");
        const std::string_view value{argv[++index]};
        if (argument == "--output-dir") result.output_directory = value;
        else if (argument == "--repetitions") {
            result.repetitions = static_cast<std::size_t>(parse_u64(value));
        } else {
            throw std::invalid_argument("unknown option: " + std::string{argument});
        }
    }
    if (result.output_directory.empty()) throw std::invalid_argument("--output-dir is required");
    if (result.repetitions < 7U) throw std::invalid_argument("at least seven repetitions required");
    return result;
}

[[nodiscard]] congruence::AffineExponentialFamily make_family(const Regime& regime) {
    congruence::AffineExponentialFamily family;
    family.k = {1, static_cast<std::int64_t>(regime.k_count), 1U};
    family.n = {0, static_cast<std::int64_t>(regime.n_count - 1U), 1U};
    family.base = 2;
    family.constant = 1;
    family.k_parity = congruence::ParityConstraint::odd;
    return family;
}

[[nodiscard]] std::vector<Variant> make_variants(const primeforge::SystemInfo& system) {
    fsieve::Options baseline;
    baseline.threads = std::max(1U, system.cpu.physical_cores);
    baseline.segment_candidates = 8'192U;
    std::vector<Variant> result{{"baseline-l2-physical", baseline}};
    const auto add = [&](std::string name, const auto change) {
        auto options = baseline;
        change(options);
        result.push_back({std::move(name), options});
    };
    add("candidate-list", [](auto& value) { value.storage = fsieve::CandidateStorage::list; });
    add("bitset-by-n", [](auto& value) { value.orientation = fsieve::BitsetOrientation::by_n; });
    add("candidate-major", [](auto& value) { value.loop_order = fsieve::LoopOrder::candidate_major; });
    add("metadata-soa", [](auto& value) { value.metadata_layout = fsieve::MetadataLayout::structure_of_arrays; });
    add("segment-l1", [](auto& value) { value.segment_candidates = 1'024U; });
    add("segment-l3", [](auto& value) { value.segment_candidates = 65'536U; });
    add("dynamic-scheduling", [](auto& value) { value.scheduling = fsieve::Scheduling::dynamic_segments; });
    add("uncompressed-classes", [](auto& value) { value.compressed_classes = false; });
    add("small-prime-wheel", [](auto& value) { value.wheel_prime_count = 4U; });
    add("bounded-crt", [](auto& value) { value.crt_prime_count = 3U; });
    add("avx2-merge", [](auto& value) { value.vector_mode = fsieve::VectorMode::avx2; });
    add("avx512-merge", [](auto& value) { value.vector_mode = fsieve::VectorMode::avx512; });
    add("explicit-prefetch", [](auto& value) { value.explicit_prefetch = true; });
    add("huge-page-probe", [](auto& value) { value.request_huge_pages = true; });
    add("physical-core-spread", [](auto& value) {
        value.thread_placement = fsieve::ThreadPlacement::physical_core_spread;
    });
    add("logical-processor-spread", [](auto& value) {
        value.thread_placement = fsieve::ThreadPlacement::logical_processor_spread;
    });
    add("logical-smt", [&](auto& value) {
        value.threads = std::max(1U, system.cpu.logical_cores);
    });
    return result;
}

[[nodiscard]] const char* yes_no(const bool value) noexcept { return value ? "YES" : "NO"; }

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("cannot create output file: " + path.string());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("cannot write output file: " + path.string());
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        std::filesystem::create_directories(arguments.output_directory);
        const primeforge::PortableSha256Provider sha256;
        const auto system = primeforge::collect_system_info();
        const std::vector<Regime> regimes{
            {"small", 64U, 16U}, {"medium", 256U, 16U}, {"large", 1'024U, 16U}};
        const auto variants = make_variants(system);
        const auto primes = primeforge::sieve::generate_primes_reference(2U, 32U).primes;
        std::vector<Sample> samples;

        for (std::size_t regime_index = 0U; regime_index < regimes.size(); ++regime_index) {
            const auto& regime = regimes[regime_index];
            const auto family = make_family(regime);
            const auto reference = fsieve::reference_eliminated_words(family, primes);
            std::string expected_hash;
            for (const auto& variant : variants) {
                const auto table = congruence::compile_congruences(family, primes, {}, sha256);
                const auto warmup = fsieve::run(table, sha256, variant.options);
                if (warmup.eliminated_words != reference) {
                    throw std::runtime_error("warmup disagrees with reference: " + variant.name);
                }
                const auto hash = fsieve::result_sha256(warmup, sha256);
                if (expected_hash.empty()) expected_hash = hash;
                if (hash != expected_hash) throw std::runtime_error("warmup result hash divergence");
            }

            const auto schedule = benchmark::randomized_variant_schedule(
                variants.size(), arguments.repetitions, 0x5354414745313000ULL + regime_index);
            for (std::size_t position = 0U; position < schedule.size(); ++position) {
                const auto variant_index = schedule[position];
                const auto& variant = variants[variant_index];
                const auto repetition = position / variants.size() + 1U;
                const auto order = position % variants.size() + 1U;
                const auto started = std::chrono::steady_clock::now();
                const auto table = congruence::compile_congruences(family, primes, {}, sha256);
                const auto result = fsieve::run(table, sha256, variant.options);
                const auto hash = fsieve::result_sha256(result, sha256);
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started);
                if (result.eliminated_words != reference || hash != expected_hash) {
                    throw std::runtime_error("timed result disagrees with scalar reference");
                }
                samples.push_back({
                    regime.name,
                    variant.name,
                    repetition,
                    order,
                    static_cast<std::uint64_t>(elapsed.count()),
                    result.candidate_count,
                    result.eliminated_count,
                    result.rule_checks,
                    result.modular_checks,
                    result.exact_checks,
                    result.bounded_magnitude_checks,
                    result.big_integer_checks,
                    hash,
                    result.vector_mode_applied,
                    result.crt_applied,
                    result.huge_pages_applied,
                    result.thread_pinning_applied,
                    result.affinity_workers_requested,
                    result.affinity_workers_applied,
                });
            }
        }

        std::string raw = "schema_version\tregime\tvariant\trepetition\torder\telapsed_nanoseconds\tcandidates\teliminated\trule_checks\tmodular_checks\texact_checks\tbounded_magnitude_checks\tbig_integer_checks\tresult_sha256\tvector_applied\tcrt_applied\thuge_pages_applied\tpinning_applied\taffinity_workers_requested\taffinity_workers_applied\ttelemetry_status\tperformance_valid\tperformance_claim\n";
        for (const auto& sample : samples) {
            raw += "1\t" + sample.regime + '\t' + sample.variant + '\t' +
                   std::to_string(sample.repetition) + '\t' + std::to_string(sample.order) + '\t' +
                   std::to_string(sample.elapsed_nanoseconds) + '\t' +
                   std::to_string(sample.candidates) + '\t' + std::to_string(sample.eliminated) + '\t' +
                   std::to_string(sample.rule_checks) + '\t' + std::to_string(sample.modular_checks) + '\t' +
                   std::to_string(sample.exact_checks) + '\t' +
                   std::to_string(sample.bounded_magnitude_checks) + '\t' +
                   std::to_string(sample.big_integer_checks) + '\t' + sample.result_sha256 + '\t' +
                   yes_no(sample.vector_applied) + '\t' + yes_no(sample.crt_applied) + '\t' +
                   yes_no(sample.huge_pages_applied) + '\t' + yes_no(sample.pinning_applied) +
                   '\t' + std::to_string(sample.affinity_workers_requested) +
                   '\t' + std::to_string(sample.affinity_workers_applied) +
                   "\tUNAVAILABLE\tNO\tNONE\n";
        }
        write_text(arguments.output_directory / "raw.tsv", raw);

        std::string summary = "schema_version\tregime\tvariant\tsamples\tminimum_nanoseconds\tmedian_nanoseconds\tmaximum_nanoseconds\tmedian_absolute_deviation_nanoseconds\tconfidence_low_nanoseconds\tconfidence_high_nanoseconds\ttelemetry_status\tperformance_valid\tperformance_claim\n";
        for (const auto& regime : regimes) {
            for (const auto& variant : variants) {
                std::vector<std::uint64_t> durations;
                for (const auto& sample : samples) {
                    if (sample.regime == regime.name && sample.variant == variant.name) {
                        durations.push_back(sample.elapsed_nanoseconds);
                    }
                }
                const auto statistics = benchmark::summarize(durations);
                summary += "1\t" + regime.name + '\t' + variant.name + '\t' +
                           std::to_string(durations.size()) + '\t' +
                           std::to_string(statistics.minimum) + '\t' +
                           std::to_string(statistics.median) + '\t' +
                           std::to_string(statistics.maximum) + '\t' +
                           std::to_string(statistics.median_absolute_deviation) + '\t' +
                           std::to_string(statistics.confidence_low) + '\t' +
                           std::to_string(statistics.confidence_high) +
                           "\tUNAVAILABLE\tNO\tNONE\n";
            }
        }
        write_text(arguments.output_directory / "summary.tsv", summary);
        std::cout << "stage10_samples=" << samples.size() << '\n'
                  << "variants=" << variants.size() << '\n'
                  << "regimes=" << regimes.size() << '\n'
                  << "telemetry_status=UNAVAILABLE\n"
                  << "performance_claim=NONE\n"
                  << "PrimeForge stage 10 experiment harness: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge stage 10 experiment harness: FAIL: " << error.what() << '\n';
        return 1;
    }
}
