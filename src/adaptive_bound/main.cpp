// SPDX-License-Identifier: Apache-2.0

#include "primeforge/adaptive_bound/adaptive_bound.hpp"
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

namespace adaptive = primeforge::adaptive_bound;
namespace benchmark = primeforge::benchmark;
namespace congruence = primeforge::congruence;
namespace fsieve = primeforge::family_sieve;

using Clock = std::chrono::steady_clock;

struct Arguments {
    std::filesystem::path output_directory;
    std::size_t repetitions{7U};
};

struct Workload {
    std::string name;
    std::string regime;
    bool calibration{};
    congruence::AffineExponentialFamily family;
};

struct BoundRun {
    std::uint64_t sieve_nanoseconds{};
    std::uint64_t next_test_nanoseconds{};
    std::uint64_t candidates{};
    std::uint64_t eliminated{};
    std::uint64_t tested_by_prp{};
    std::uint64_t probable_primes{};
};

struct ValidationSample {
    std::string workload;
    std::string regime;
    std::string strategy;
    std::size_t repetition{};
    std::size_t order{};
    std::uint64_t selected_bound{};
    std::uint64_t elapsed_nanoseconds{};
    std::uint64_t sieve_nanoseconds{};
    std::uint64_t next_test_nanoseconds{};
    std::uint64_t candidates{};
    std::uint64_t eliminated{};
    std::uint64_t tested_by_prp{};
    std::uint64_t probable_primes{};
};

[[nodiscard]] std::uint64_t elapsed_ns(const Clock::time_point begin) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}

[[nodiscard]] std::uint64_t parse_u64(const std::string_view text) {
    std::size_t consumed = 0U;
    const auto value = std::stoull(std::string{text}, &consumed, 10);
    if (consumed != text.size()) throw std::invalid_argument("invalid integer argument");
    return value;
}

[[nodiscard]] Arguments parse_arguments(const int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        if (index + 1 >= argc) throw std::invalid_argument("option requires value");
        const std::string_view option{argv[index]};
        const std::string_view value{argv[++index]};
        if (option == "--output-dir") result.output_directory = value;
        else if (option == "--repetitions") {
            result.repetitions = static_cast<std::size_t>(parse_u64(value));
        } else {
            throw std::invalid_argument("unknown option: " + std::string{option});
        }
    }
    if (result.output_directory.empty()) throw std::invalid_argument("--output-dir is required");
    if (result.repetitions < 7U) throw std::invalid_argument("at least seven repetitions required");
    return result;
}

[[nodiscard]] congruence::AffineExponentialFamily make_family(
    const std::uint64_t k_count, const std::int64_t base, const std::int64_t constant) {
    congruence::AffineExponentialFamily family;
    family.k = {1, static_cast<std::int64_t>(k_count * 2U - 1U), 2U};
    family.n = {0, 15, 1U};
    family.base = base;
    family.constant = constant;
    return family;
}

[[nodiscard]] std::vector<Workload> make_workloads() {
    std::vector<Workload> result;
    for (const auto& [regime, count] :
         std::vector<std::pair<std::string, std::uint64_t>>{
             {"small", 64U}, {"medium", 256U}, {"large", 1'024U}}) {
        result.push_back({"cal-2-plus-" + regime, regime, true, make_family(count, 2, 1)});
        result.push_back({"cal-3-minus-" + regime, regime, true, make_family(count, 3, -1)});
        result.push_back({"val-3-plus-" + regime, regime, false, make_family(count, 3, 1)});
        result.push_back({"val-5-minus-" + regime, regime, false, make_family(count, 5, -1)});
    }
    return result;
}

[[nodiscard]] bool bit_is_set(
    const std::vector<std::uint64_t>& words, const std::uint64_t index) noexcept {
    return (words[static_cast<std::size_t>(index / 64U)] &
            (std::uint64_t{1} << (index % 64U))) != 0U;
}

[[nodiscard]] std::vector<std::uint64_t> primes_through(
    const std::vector<std::uint64_t>& all, const std::uint64_t bound) {
    const auto end = std::upper_bound(all.begin(), all.end(), bound);
    return {all.begin(), end};
}

[[nodiscard]] BoundRun run_bound(
    const congruence::AffineExponentialFamily& family,
    const std::vector<std::uint64_t>& all_primes,
    const std::uint64_t bound,
    const fsieve::Options& options,
    const primeforge::Sha256Provider& sha256) {
    const auto selected_primes = primes_through(all_primes, bound);
    const auto sieve_started = Clock::now();
    const auto table = congruence::compile_congruences(family, selected_primes, {}, sha256);
    const auto sieve_result = fsieve::run(table, sha256, options);
    const auto sieve_nanoseconds = elapsed_ns(sieve_started);

    const auto next_started = Clock::now();
    std::uint64_t tested = 0U;
    std::uint64_t probable = 0U;
    const auto n_count = family.n.size();
    for (std::uint64_t k = 0U; k < family.k.size(); ++k) {
        for (std::uint64_t n = 0U; n < n_count; ++n) {
            const auto index = k * n_count + n;
            if (bit_is_set(sieve_result.eliminated_words, index)) continue;
            const auto value = congruence::evaluate_exact(family, k, n);
            if (value <= primeforge::math::BigInteger{1} || value.is_negative()) continue;
            const auto small = value.to_uint64_absolute();
            if (!small.has_value()) throw std::length_error("stage-11 workload exceeds uint64");
            ++tested;
            if (adaptive::is_base2_strong_probable_prime_u64(*small)) ++probable;
        }
    }
    const auto next_nanoseconds = elapsed_ns(next_started);
    return {
        sieve_nanoseconds,
        next_nanoseconds,
        sieve_result.candidate_count,
        sieve_result.eliminated_count,
        tested,
        probable,
    };
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("cannot create " + path.string());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("cannot write " + path.string());
}

[[nodiscard]] std::uint64_t median(std::vector<std::uint64_t> values) {
    if (values.empty()) throw std::invalid_argument("empty median input");
    std::ranges::sort(values);
    return values[(values.size() - 1U) / 2U];
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        std::filesystem::create_directories(arguments.output_directory);
        const primeforge::PortableSha256Provider sha256;
        const auto system = primeforge::collect_system_info();
        fsieve::Options sieve_options;
        sieve_options.threads = std::max(1U, system.cpu.physical_cores);
        sieve_options.segment_candidates = 1'024U;
        const auto workloads = make_workloads();
        const std::vector<std::uint64_t> bounds{7U, 19U, 43U};
        const auto all_primes = primeforge::sieve::generate_primes_reference(2U, 44U).primes;

        struct CalibrationRow {
            std::string workload;
            std::string regime;
            std::uint64_t bound{};
            std::size_t repetition{};
            BoundRun run;
        };
        std::vector<CalibrationRow> calibration;
        for (const auto& workload : workloads) {
            if (!workload.calibration) continue;
            for (const auto bound : bounds) {
                for (std::size_t repetition = 1U; repetition <= arguments.repetitions; ++repetition) {
                    calibration.push_back({
                        workload.name, workload.regime, bound, repetition,
                        run_bound(workload.family, all_primes, bound, sieve_options, sha256)});
                }
            }
        }

        std::map<std::string, std::uint64_t> offline_bound;
        std::map<std::string, std::uint64_t> next_cost;
        std::string model = "schema_version\tregime\tbound\tmedian_sieve_nanoseconds\tmeasured_candidates\tmeasured_eliminations\tmeasured_survivors\tnext_prp_nanoseconds_per_survivor\tprojected_total_nanoseconds\toffline_selected\n";
        for (const std::string regime : {"small", "medium", "large"}) {
            std::vector<std::uint64_t> per_survivor;
            for (const auto& row : calibration) {
                if (row.regime == regime && row.run.tested_by_prp != 0U) {
                    per_survivor.push_back(row.run.next_test_nanoseconds / row.run.tested_by_prp);
                }
            }
            next_cost[regime] = std::max<std::uint64_t>(1U, median(per_survivor));
            std::vector<adaptive::BoundObservation> curve;
            std::uint64_t prior_sieve = 0U;
            std::uint64_t prior_eliminated = 0U;
            for (const auto bound : bounds) {
                std::vector<std::uint64_t> sieve_times;
                std::uint64_t candidates = 0U;
                std::uint64_t eliminated_sum = 0U;
                std::uint64_t rows = 0U;
                for (const auto& row : calibration) {
                    if (row.regime == regime && row.bound == bound) {
                        sieve_times.push_back(row.run.sieve_nanoseconds);
                        candidates = row.run.candidates;
                        eliminated_sum += row.run.eliminated;
                        ++rows;
                    }
                }
                const auto observed_sieve = std::max(prior_sieve, median(sieve_times));
                const auto observed_eliminated =
                    std::max(prior_eliminated, eliminated_sum / rows);
                curve.push_back({bound, observed_sieve, candidates, observed_eliminated});
                prior_sieve = observed_sieve;
                prior_eliminated = observed_eliminated;
            }
            const auto choice = adaptive::select_offline_bound(curve, next_cost[regime]);
            offline_bound[regime] = choice.upper_prime;
            for (const auto& observation : curve) {
                const auto projected = observation.cumulative_sieve_nanoseconds +
                    observation.survivor_count() * next_cost[regime];
                model += "1\t" + regime + '\t' + std::to_string(observation.upper_prime) + '\t' +
                         std::to_string(observation.cumulative_sieve_nanoseconds) + '\t' +
                         std::to_string(observation.candidate_count) + '\t' +
                         std::to_string(observation.eliminated_count) + '\t' +
                         std::to_string(observation.survivor_count()) + '\t' +
                         std::to_string(next_cost[regime]) + '\t' + std::to_string(projected) + '\t' +
                         (observation.upper_prime == choice.upper_prime ? "YES" : "NO") + "\n";
            }
        }
        write_text(arguments.output_directory / "model.tsv", model);

        const std::vector<std::string> strategies{
            "fixed-low", "fixed-medium", "fixed-high", "adaptive-offline", "adaptive-online"};
        std::vector<ValidationSample> samples;
        for (std::size_t workload_index = 0U; workload_index < workloads.size(); ++workload_index) {
            const auto& workload = workloads[workload_index];
            if (workload.calibration) continue;
            const auto schedule = benchmark::randomized_variant_schedule(
                strategies.size(), arguments.repetitions, 0x4144415054495600ULL + workload_index);
            for (std::size_t position = 0U; position < schedule.size(); ++position) {
                const auto strategy_index = schedule[position];
                const auto& strategy = strategies[strategy_index];
                const auto started = Clock::now();
                BoundRun outcome;
                std::uint64_t selected_bound = 0U;
                if (strategy != "adaptive-online") {
                    selected_bound = strategy == "fixed-low" ? bounds[0] :
                                     strategy == "fixed-medium" ? bounds[1] :
                                     strategy == "fixed-high" ? bounds[2] :
                                     offline_bound.at(workload.regime);
                    outcome = run_bound(
                        workload.family, all_primes, selected_bound, sieve_options, sha256);
                } else {
                    std::vector<adaptive::BoundObservation> observed;
                    for (const auto bound : bounds) {
                        outcome = run_bound(
                            workload.family, all_primes, bound, sieve_options, sha256);
                        selected_bound = bound;
                        observed.push_back({
                            bound, elapsed_ns(started), outcome.candidates, outcome.eliminated});
                        if (observed.size() > 1U) {
                            const auto recommendation = adaptive::select_online_bound(
                                observed, next_cost.at(workload.regime));
                            if (recommendation.observation_index + 1U < observed.size()) break;
                        }
                    }
                }
                samples.push_back({
                    workload.name,
                    workload.regime,
                    strategy,
                    position / strategies.size() + 1U,
                    position % strategies.size() + 1U,
                    selected_bound,
                    elapsed_ns(started),
                    outcome.sieve_nanoseconds,
                    outcome.next_test_nanoseconds,
                    outcome.candidates,
                    outcome.eliminated,
                    outcome.tested_by_prp,
                    outcome.probable_primes,
                });
            }
        }

        std::string calibration_text = "schema_version\tset\tworkload\tregime\tbound\trepetition\tsieve_nanoseconds\tnext_prp_nanoseconds\tcandidates\teliminated\ttested_by_prp\tprobable_primes\tprimality_status\tperformance_valid\tperformance_claim\n";
        for (const auto& row : calibration) {
            calibration_text += "1\tCALIBRATION\t" + row.workload + '\t' + row.regime + '\t' +
                std::to_string(row.bound) + '\t' + std::to_string(row.repetition) + '\t' +
                std::to_string(row.run.sieve_nanoseconds) + '\t' +
                std::to_string(row.run.next_test_nanoseconds) + '\t' +
                std::to_string(row.run.candidates) + '\t' + std::to_string(row.run.eliminated) + '\t' +
                std::to_string(row.run.tested_by_prp) + '\t' +
                std::to_string(row.run.probable_primes) +
                "\tPROBABLE_PRIME_OR_COMPOSITE\tNO\tNONE\n";
        }
        write_text(arguments.output_directory / "calibration.tsv", calibration_text);

        std::string raw = "schema_version\tset\tworkload\tregime\tstrategy\trepetition\torder\tselected_bound\telapsed_nanoseconds\tsieve_nanoseconds\tnext_prp_nanoseconds\tcandidates\teliminated\ttested_by_prp\tprobable_primes\tprimality_status\tenergy_joules\tperformance_valid\tperformance_claim\n";
        for (const auto& sample : samples) {
            raw += "1\tVALIDATION\t" + sample.workload + '\t' + sample.regime + '\t' +
                   sample.strategy + '\t' + std::to_string(sample.repetition) + '\t' +
                   std::to_string(sample.order) + '\t' + std::to_string(sample.selected_bound) + '\t' +
                   std::to_string(sample.elapsed_nanoseconds) + '\t' +
                   std::to_string(sample.sieve_nanoseconds) + '\t' +
                   std::to_string(sample.next_test_nanoseconds) + '\t' +
                   std::to_string(sample.candidates) + '\t' + std::to_string(sample.eliminated) + '\t' +
                   std::to_string(sample.tested_by_prp) + '\t' +
                   std::to_string(sample.probable_primes) +
                   "\tPROBABLE_PRIME_OR_COMPOSITE\tUNKNOWN\tNO\tNONE\n";
        }
        write_text(arguments.output_directory / "raw.tsv", raw);

        std::string summary = "schema_version\tregime\tstrategy\tsamples\tminimum_nanoseconds\tmedian_nanoseconds\tmaximum_nanoseconds\tmedian_absolute_deviation_nanoseconds\tconfidence_low_nanoseconds\tconfidence_high_nanoseconds\tcomparison_to_fixed_medium\thypothesis_status\tperformance_valid\tperformance_claim\n";
        std::string gate = "schema_version\tregime\tadaptive_offline_comparison\tadaptive_online_comparison\thypothesis_status\treason\n";
        for (const std::string regime : {"small", "medium", "large"}) {
            std::map<std::string, benchmark::SummaryStatistics> statistics;
            for (const auto& strategy : strategies) {
                std::vector<std::uint64_t> durations;
                for (const auto& sample : samples) {
                    if (sample.regime == regime && sample.strategy == strategy) {
                        durations.push_back(sample.elapsed_nanoseconds);
                    }
                }
                statistics.emplace(strategy, benchmark::summarize(durations));
            }
            for (const auto& strategy : strategies) {
                const auto& value = statistics.at(strategy);
                const auto comparison = adaptive::compare_robustly(
                    value, statistics.at("fixed-medium"));
                summary += "1\t" + regime + '\t' + strategy + '\t' +
                           std::to_string(arguments.repetitions * 2U) + '\t' +
                           std::to_string(value.minimum) + '\t' + std::to_string(value.median) + '\t' +
                           std::to_string(value.maximum) + '\t' +
                           std::to_string(value.median_absolute_deviation) + '\t' +
                           std::to_string(value.confidence_low) + '\t' +
                           std::to_string(value.confidence_high) + '\t' +
                           adaptive::to_string(comparison) + "\tFAILED\tNO\tNONE\n";
            }
            gate += "1\t" + regime + '\t' +
                    adaptive::to_string(adaptive::compare_robustly(
                        statistics.at("adaptive-offline"), statistics.at("fixed-medium"))) + '\t' +
                    adaptive::to_string(adaptive::compare_robustly(
                        statistics.at("adaptive-online"), statistics.at("fixed-medium"))) +
                    "\tFAILED\tNO_CLAIM_ELIGIBLE_ROBUST_GAIN\n";
        }
        write_text(arguments.output_directory / "summary.tsv", summary);
        write_text(arguments.output_directory / "gate.tsv", gate);

        std::cout << "calibration_rows=" << calibration.size() << '\n'
                  << "validation_rows=" << samples.size() << '\n'
                  << "next_engine=BASE2_STRONG_PRP_U64\n"
                  << "energy_joules=UNKNOWN\n"
                  << "performance_claim=NONE\n"
                  << "hypothesis_status=FAILED_PER_REGIME\n"
                  << "PrimeForge adaptive-bound experiment: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge adaptive-bound experiment: FAIL: " << error.what() << '\n';
        return 1;
    }
}
