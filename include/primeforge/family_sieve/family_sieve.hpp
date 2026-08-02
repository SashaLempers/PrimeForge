// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/congruence/compiler.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace primeforge::family_sieve {

enum class CandidateStorage { list, dense_bitset };
enum class BitsetOrientation { by_k, by_n };
enum class LoopOrder { prime_major, candidate_major };
enum class MetadataLayout { array_of_structures, structure_of_arrays };
enum class Scheduling { static_partition, dynamic_segments };
enum class VectorMode { scalar, avx2, avx512 };
enum class ThreadPlacement {
    scheduler_managed,
    physical_core_spread,
    logical_processor_spread
};

struct Options {
    CandidateStorage storage{CandidateStorage::dense_bitset};
    BitsetOrientation orientation{BitsetOrientation::by_k};
    LoopOrder loop_order{LoopOrder::prime_major};
    MetadataLayout metadata_layout{MetadataLayout::array_of_structures};
    Scheduling scheduling{Scheduling::static_partition};
    VectorMode vector_mode{VectorMode::scalar};
    ThreadPlacement thread_placement{ThreadPlacement::scheduler_managed};
    std::uint64_t segment_candidates{8'192U};
    unsigned int threads{1U};
    bool compressed_classes{true};
    std::size_t wheel_prime_count{};
    std::size_t crt_prime_count{};
    std::uint64_t crt_memory_limit_bytes{8U * 1024U * 1024U};
    bool explicit_prefetch{};
    bool request_huge_pages{};
};

struct Result {
    // Canonical k-major bitset. A set bit means a locally witnessed proper factor.
    std::vector<std::uint64_t> eliminated_words;
    std::uint64_t candidate_count{};
    std::uint64_t eliminated_count{};
    std::uint64_t rule_checks{};
    std::uint64_t modular_checks{};
    std::uint64_t exact_checks{};
    std::uint64_t bounded_magnitude_checks{};
    std::uint64_t big_integer_checks{};
    bool vector_mode_applied{};
    bool crt_applied{};
    bool huge_pages_applied{};
    bool thread_pinning_applied{};
    unsigned int affinity_workers_requested{};
    unsigned int affinity_workers_applied{};
};

[[nodiscard]] Result run(
    const congruence::CompiledTable& table,
    const Sha256Provider& sha256,
    const Options& options);

[[nodiscard]] std::vector<std::uint64_t> reference_eliminated_words(
    const congruence::AffineExponentialFamily& family,
    const std::vector<std::uint64_t>& primes);

[[nodiscard]] std::string result_sha256(const Result& result, const Sha256Provider& sha256);
[[nodiscard]] std::string describe(const Options& options);

}  // namespace primeforge::family_sieve
