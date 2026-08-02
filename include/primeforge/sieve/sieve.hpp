// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace primeforge::sieve {

enum class BucketLayout {
    structure_of_arrays,
    array_of_structures
};

struct SieveOptions {
    std::uint64_t segment_span{1U << 20U};
    std::size_t thread_count{1};
    bool wheel_modulo_30{false};
    bool bit_packed{false};
    bool bucket_sieve{false};
    bool prefetch{false};
    BucketLayout bucket_layout{BucketLayout::structure_of_arrays};
};

struct SieveResult {
    std::uint64_t begin{};
    std::uint64_t end{};
    std::vector<std::uint64_t> primes;
};

[[nodiscard]] SieveResult generate_primes_reference(std::uint64_t begin, std::uint64_t end);
[[nodiscard]] SieveResult generate_primes(
    std::uint64_t begin, std::uint64_t end, const SieveOptions& options = {});
[[nodiscard]] std::uint64_t count_primes(
    std::uint64_t begin, std::uint64_t end, const SieveOptions& options = {});
[[nodiscard]] bool is_prime_u64(std::uint64_t value) noexcept;
[[nodiscard]] std::string_view to_string(BucketLayout layout) noexcept;

}  // namespace primeforge::sieve
