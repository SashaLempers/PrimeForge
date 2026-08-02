// SPDX-License-Identifier: Apache-2.0

#include "vector_merge.hpp"

namespace primeforge::family_sieve::detail {

void merge_words_scalar(
    std::uint64_t* const output,
    const std::uint64_t* const input,
    const std::size_t count) noexcept {
    for (std::size_t index = 0U; index < count; ++index) output[index] |= input[index];
}

}  // namespace primeforge::family_sieve::detail
