// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace primeforge::family_sieve::detail {

void merge_words_scalar(std::uint64_t* output, const std::uint64_t* input, std::size_t count) noexcept;
void merge_words_avx2(std::uint64_t* output, const std::uint64_t* input, std::size_t count) noexcept;
void merge_words_avx512(std::uint64_t* output, const std::uint64_t* input, std::size_t count) noexcept;

}  // namespace primeforge::family_sieve::detail
