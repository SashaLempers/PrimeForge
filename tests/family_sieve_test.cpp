// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/core/system_info.hpp"
#include "primeforge/family_sieve/family_sieve.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

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

        fsieve::Options baseline;
        baseline.threads = 2U;
        baseline.segment_candidates = 128U;
        const auto expected = fsieve::run(table, sha256, baseline);
        check(expected.eliminated_words == reference, "baseline equals direct scalar reference");
        check(expected.eliminated_count != 0U, "test family has eliminations");

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
        add([](auto& value) { value.thread_placement = fsieve::ThreadPlacement::pinned; });
        add([](auto& value) { value.threads = 1U; });
        add([](auto& value) { value.threads = 4U; });

        std::uint64_t variant_index = 0U;
        for (const auto& options : variants) {
            const auto result = fsieve::run(table, sha256, options);
            check(result.eliminated_words == reference,
                  "one-factor variant equals reference: " + fsieve::describe(options));
            check(result.candidate_count == family.k.size() * family.n.size(),
                  "candidate count retained");
            ++variant_index;
        }
        const auto crt_result = fsieve::run(table, sha256, variants[10]);
        check(crt_result.crt_applied, "bounded CRT residue template was applied");

        const auto capabilities = primeforge::collect_system_info().cpu;
        const auto avx2_result = fsieve::run(table, sha256, variants[11]);
        const auto avx512_result = fsieve::run(table, sha256, variants[12]);
        check(avx2_result.vector_mode_applied == capabilities.avx2,
              "AVX2 dispatch follows runtime capability");
        check(avx512_result.vector_mode_applied == capabilities.avx512f,
              "AVX-512 dispatch follows runtime capability");

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
                  << "result_sha256=" << first_hash << '\n'
                  << "PrimeForge family sieve tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge family sieve tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
