// SPDX-License-Identifier: Apache-2.0

#include "vector_merge.hpp"

#include <immintrin.h>

namespace primeforge::family_sieve::detail {

void merge_words_avx512(
    std::uint64_t* const output,
    const std::uint64_t* const input,
    const std::size_t count) noexcept {
    std::size_t index = 0U;
    for (; index + 8U <= count; index += 8U) {
        const auto input_value = _mm512_loadu_si512(input + index);
        const auto output_value = _mm512_loadu_si512(output + index);
        _mm512_storeu_si512(output + index, _mm512_or_si512(output_value, input_value));
    }
    for (; index < count; ++index) output[index] |= input[index];
}

}  // namespace primeforge::family_sieve::detail
