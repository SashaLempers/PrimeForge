// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/core/system_info.hpp"
#include "primeforge/cpu/cpu_topology.hpp"
#include "primeforge/family_sieve/family_sieve.hpp"
#include "primeforge/math/big_integer.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace congruence = primeforge::congruence;
namespace fsieve = primeforge::family_sieve;

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_failure(Function&& function, const std::string& message) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}

[[nodiscard]] primeforge::congruence::AffineExponentialFamily test_family() {
    primeforge::congruence::AffineExponentialFamily family;
    family.k = {-20, 83, 1U};
    family.n = {0, 15, 1U};
    family.base = 2;
    family.constant = 1;
    return family;
}

void check_factor_witnesses(
    const fsieve::Result& result,
    const congruence::AffineExponentialFamily& family,
    const std::string& context) {
    const auto n_count = family.n.size();
    check(result.factor_witnesses.size() == result.candidate_count,
          context + ": complete factor-witness vector");
    for (std::uint64_t index = 0U; index < result.candidate_count; ++index) {
        const auto word = result.eliminated_words[static_cast<std::size_t>(index / 64U)];
        const bool eliminated =
            (word & (std::uint64_t{1} << (index % 64U))) != 0U;
        const auto factor = result.factor_witnesses[static_cast<std::size_t>(index)];
        check(eliminated == (factor != 0U), context + ": witness/bitset agreement");
        if (factor == 0U) continue;
        const auto value = congruence::evaluate_exact(
            family, index / n_count, index % n_count);
        check(value > primeforge::math::BigInteger{1} && value.modulo(factor) == 0U,
              context + ": retained factor divides candidate");
        check(value > primeforge::math::BigInteger::from_decimal(std::to_string(factor)),
              context + ": retained factor is proper");
    }
}

[[nodiscard]] std::vector<std::uint64_t> reference_factor_witnesses(
    const congruence::AffineExponentialFamily& family,
    const std::vector<std::uint64_t>& primes) {
    std::vector<std::uint64_t> result(
        static_cast<std::size_t>(family.k.size() * family.n.size()), 0U);
    for (const auto& elimination :
         congruence::scalar_reference_eliminations(family, primes)) {
        const auto index = elimination.candidate.k_index * family.n.size() +
                           elimination.candidate.n_index;
        const auto factor = congruence::reconstruct_factor(elimination);
        auto& retained = result[static_cast<std::size_t>(index)];
        if (retained == 0U || factor < retained) retained = factor;
    }
    return result;
}

}  // namespace

int main() {
    try {
        const primeforge::PortableSha256Provider sha256;
        const std::vector<std::uint64_t> primes{
            2U, 3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U, 31U, 37U, 41U, 43U};
        const auto family = test_family();
        const auto table = primeforge::congruence::compile_congruences(
            family, primes, {}, sha256);
        const auto reference = fsieve::reference_eliminated_words(family, primes);
        const auto reference_factors = reference_factor_witnesses(family, primes);

        fsieve::Options baseline;
        baseline.threads = 2U;
        baseline.segment_candidates = 128U;
        baseline.retain_factor_witnesses = true;
        const auto expected = fsieve::run(table, sha256, baseline);
        check(expected.eliminated_words == reference, "baseline equals direct scalar reference");
        check(expected.direct_bitset_writes_applied,
              "word-aligned dense baseline writes disjoint result words directly");
        check(expected.eliminated_count != 0U, "test family has eliminations");
        check_factor_witnesses(expected, family, "baseline");
        check(expected.factor_witnesses == reference_factors,
              "baseline retains the canonical smallest reference factor");

        auto concurrent_direct = baseline;
        concurrent_direct.threads = 16U;
        concurrent_direct.segment_candidates = 64U;
        concurrent_direct.scheduling = fsieve::Scheduling::dynamic_segments;
        for (std::size_t repetition = 0U; repetition < 20U; ++repetition) {
            const auto concurrent_result =
                fsieve::run(table, sha256, concurrent_direct);
            check(concurrent_result.direct_bitset_writes_applied,
                  "dynamic aligned segments keep direct writes enabled");
            check(concurrent_result.eliminated_words == reference &&
                      concurrent_result.factor_witnesses == reference_factors,
                  "concurrent direct writes remain deterministic and canonical");
        }

        std::vector<fsieve::Options> variants;
        auto add = [&](const auto change) {
            auto options = baseline;
            change(options);
            variants.push_back(options);
        };
        add([](auto& value) { value.storage = fsieve::CandidateStorage::list; });
        add([](auto& value) { value.orientation = fsieve::BitsetOrientation::by_n; });
        add([](auto& value) { value.loop_order = fsieve::LoopOrder::candidate_major; });
        add([](auto& value) { value.metadata_layout = fsieve::MetadataLayout::structure_of_arrays; });
        add([](auto& value) { value.segment_candidates = 64U; });
        add([](auto& value) { value.segment_candidates = 512U; });
        add([](auto& value) { value.segment_candidates = 4'096U; });
        add([](auto& value) { value.scheduling = fsieve::Scheduling::dynamic_segments; });
        add([](auto& value) { value.compressed_classes = false; });
        add([](auto& value) { value.wheel_prime_count = 4U; });
        add([](auto& value) { value.crt_prime_count = 3U; });
        add([](auto& value) { value.vector_mode = fsieve::VectorMode::avx2; });
        add([](auto& value) { value.vector_mode = fsieve::VectorMode::avx512; });
        add([](auto& value) { value.explicit_prefetch = true; });
        add([](auto& value) { value.request_huge_pages = true; });
        add([](auto& value) {
            value.thread_placement = fsieve::ThreadPlacement::physical_core_spread;
        });
        add([](auto& value) {
            value.thread_placement = fsieve::ThreadPlacement::logical_processor_spread;
        });
        add([](auto& value) { value.threads = 1U; });
        add([](auto& value) { value.threads = 4U; });
        add([](auto& value) { value.segment_candidates = 127U; });

        std::uint64_t variant_index = 0U;
        for (const auto& options : variants) {
            const auto result = fsieve::run(table, sha256, options);
            check(result.eliminated_words == reference,
                  "one-factor variant equals reference: " + fsieve::describe(options));
            check(result.candidate_count == family.k.size() * family.n.size(),
                  "candidate count retained");
            const auto description = fsieve::describe(options);
            check_factor_witnesses(result, family, description);
            check(result.factor_witnesses == reference_factors,
                  description + ": retains identical canonical factors");
            ++variant_index;
        }
        const auto crt_result = fsieve::run(table, sha256, variants[10]);
        check(crt_result.crt_applied, "bounded CRT residue template was applied");

        congruence::AffineExponentialFamily positive_family;
        positive_family.k = {1, 127, 1U};
        positive_family.n = {0, 20, 1U};
        positive_family.base = 2;
        positive_family.constant = 1;
        const auto positive_table = congruence::compile_congruences(
            positive_family, primes, {}, sha256);
        const auto positive_result = fsieve::run(positive_table, sha256, baseline);
        check(positive_result.eliminated_words ==
                  fsieve::reference_eliminated_words(positive_family, primes),
              "bounded magnitude path equals the arbitrary-precision reference");
        check(positive_result.exact_checks != 0U &&
                  positive_result.bounded_magnitude_checks == positive_result.exact_checks &&
                  positive_result.big_integer_checks == 0U,
              "nonnegative family avoids every BigInteger magnitude check");

        auto signed_family = positive_family;
        signed_family.constant = -1;
        const auto signed_table = congruence::compile_congruences(
            signed_family, primes, {}, sha256);
        const auto signed_result = fsieve::run(signed_table, sha256, baseline);
        check(signed_result.eliminated_words ==
                  fsieve::reference_eliminated_words(signed_family, primes),
              "signed fallback equals the arbitrary-precision reference");
        check(signed_result.exact_checks != 0U &&
                  signed_result.big_integer_checks == signed_result.exact_checks &&
                  signed_result.bounded_magnitude_checks == 0U,
              "signed family conservatively retains every BigInteger check");
        check(expected.exact_checks == expected.bounded_magnitude_checks +
                  expected.big_integer_checks,
              "mixed family accounts for every exact magnitude check");

        auto without_witnesses = baseline;
        without_witnesses.retain_factor_witnesses = false;
        check(fsieve::run(table, sha256, without_witnesses).factor_witnesses.empty(),
              "factor witnesses remain opt-in");

        const auto capabilities = primeforge::collect_system_info().cpu;
        const auto avx2_result = fsieve::run(table, sha256, variants[11]);
        const auto avx512_result = fsieve::run(table, sha256, variants[12]);
        check(!avx2_result.vector_mode_applied && !avx512_result.vector_mode_applied,
              "direct bitset writes remove the SIMD merge entirely");
        const auto unaligned_result = fsieve::run(table, sha256, variants.back());
        check(!unaligned_result.direct_bitset_writes_applied,
              "non-word-aligned segments retain the safe worker-local fallback");
        check(!fsieve::run(table, sha256, variants[1]).direct_bitset_writes_applied,
              "transposed traversal retains the safe worker-local fallback");
        auto unaligned_avx2 = variants.back();
        unaligned_avx2.vector_mode = fsieve::VectorMode::avx2;
        auto unaligned_avx512 = variants.back();
        unaligned_avx512.vector_mode = fsieve::VectorMode::avx512;
        check(fsieve::run(table, sha256, unaligned_avx2).vector_mode_applied ==
                  capabilities.avx2,
              "fallback AVX2 merge follows runtime capability");
        check(fsieve::run(table, sha256, unaligned_avx512).vector_mode_applied ==
                  capabilities.avx512f,
              "fallback AVX-512 merge follows runtime capability");
        const auto physical_affinity = fsieve::run(table, sha256, variants[15]);
        const auto logical_affinity = fsieve::run(table, sha256, variants[16]);
        check(physical_affinity.affinity_workers_requested == baseline.threads,
              "physical-core placement records every requested worker");
        check(logical_affinity.affinity_workers_requested == baseline.threads,
              "logical placement records every requested worker");
#ifdef _WIN32
        const auto topology = primeforge::cpu::collect_topology();
        const auto physical_plan = primeforge::cpu::build_affinity_plan(
            topology.cpu_sets,
            primeforge::cpu::AffinityStrategy::physical_core_spread,
            baseline.threads);
        const auto logical_plan = primeforge::cpu::build_affinity_plan(
            topology.cpu_sets,
            primeforge::cpu::AffinityStrategy::logical_processor_spread,
            baseline.threads);
        check(physical_affinity.affinity_workers_applied <=
                  std::min<std::size_t>(
                      physical_affinity.affinity_workers_requested, physical_plan.size()),
              "physical placement never fabricates an applied CPU set");
        check(logical_affinity.affinity_workers_applied <=
                  std::min<std::size_t>(
                      logical_affinity.affinity_workers_requested, logical_plan.size()),
              "logical placement never fabricates an applied CPU set");
        check(physical_affinity.thread_pinning_applied ==
                  (physical_affinity.affinity_workers_applied != 0U),
              "physical placement summary matches the exact applied count");
        check(logical_affinity.thread_pinning_applied ==
                  (logical_affinity.affinity_workers_applied != 0U),
              "logical placement summary matches the exact applied count");

        fsieve::Result target_affinity_result;
        const bool target_affinity_checked =
            capabilities.brand.find("AMD Ryzen 9 9950X3D") != std::string::npos &&
            capabilities.physical_cores == 16U;
        if (target_affinity_checked) {
            auto target_options = baseline;
            target_options.threads = 16U;
            target_options.segment_candidates = 64U;
            target_options.thread_placement =
                fsieve::ThreadPlacement::physical_core_spread;
            target_affinity_result = fsieve::run(table, sha256, target_options);
            check(target_affinity_result.eliminated_words == reference,
                  "target 16-core placement equals the scalar reference");
            check(target_affinity_result.affinity_workers_requested == 16U &&
                      target_affinity_result.affinity_workers_applied == 16U,
                  "Ryzen 9 9950X3D applies the complete 16-core plan");
        }
#else
        check(physical_affinity.affinity_workers_applied == 0U &&
                  logical_affinity.affinity_workers_applied == 0U,
              "portable CI does not fabricate CPU-set affinity");
#endif

        const auto first_hash = fsieve::result_sha256(expected, sha256);
        const auto second_hash = fsieve::result_sha256(
            fsieve::run(table, sha256, baseline), sha256);
        check(first_hash.size() == 64U && first_hash == second_hash,
              "canonical result digest is deterministic");

        auto invalid = baseline;
        invalid.threads = 0U;
        expect_failure(
            [&] { static_cast<void>(fsieve::run(table, sha256, invalid)); },
            "zero threads rejected");
        auto corrupted = table;
        corrupted.table_sha256[0] = corrupted.table_sha256[0] == '0' ? '1' : '0';
        expect_failure(
            [&] { static_cast<void>(fsieve::run(corrupted, sha256, baseline)); },
            "corrupted compiled table rejected");

        std::cout << "family_sieve_variants_checked=" << variant_index << '\n'
                  << "candidate_count=" << expected.candidate_count << '\n'
                  << "eliminated_count=" << expected.eliminated_count << '\n'
                  << "bounded_magnitude_checks=" << positive_result.bounded_magnitude_checks << '\n'
                  << "big_integer_checks_nonnegative=" << positive_result.big_integer_checks << '\n'
                  << "big_integer_checks_signed=" << signed_result.big_integer_checks << '\n'
#ifdef _WIN32
                  << "physical_plan_workers=" << physical_plan.size() << '\n'
                  << "physical_affinity_workers_applied="
                  << physical_affinity.affinity_workers_applied << '\n'
                  << "logical_plan_workers=" << logical_plan.size() << '\n'
                  << "logical_affinity_workers_applied="
                  << logical_affinity.affinity_workers_applied << '\n'
                  << "target_16_core_affinity_checked="
                  << (target_affinity_checked ? "YES" : "NO") << '\n'
                  << "target_16_core_affinity_applied="
                  << target_affinity_result.affinity_workers_applied << '\n'
#endif
                  << "result_sha256=" << first_hash << '\n'
                  << "PrimeForge family sieve tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge family sieve tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
