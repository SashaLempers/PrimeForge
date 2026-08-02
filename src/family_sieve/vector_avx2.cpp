// SPDX-License-Identifier: Apache-2.0

#include "vector_merge.hpp"

#include <immintrin.h>

namespace primeforge::family_sieve::detail {

void merge_words_avx2(
    std::uint64_t* const output,
    const std::uint64_t* const input,
    const std::size_t count) noexcept {
    std::size_t index = 0U;
    for (; index + 4U <= count; index += 4U) {
        const auto input_value = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(input + index));
        const auto output_value = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(output + index));
        _mm256_storeu_si256(
            reinterpret_cast<__m256i*>(output + index),
            _mm256_or_si256(output_value, input_value));
    }
    for (; index < count; ++index) output[index] |= input[index];
}

}  // namespace primeforge::family_sieve::detail
