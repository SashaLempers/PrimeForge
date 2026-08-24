// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/detail/wide_montgomery_avx512.hpp"

#include <immintrin.h>

namespace primeforge::discovery::detail {
namespace {

constexpr std::uint64_t radix = 1ULL << 52U;
constexpr std::uint64_t radix_mask = radix - 1U;

[[nodiscard]] __m512i montgomery_multiply(
    const __m512i left, const __m512i right, const __m512i modulus,
    const __m512i negative_inverse) noexcept {
    // IFMA exposes the low and high 52-bit limbs of eight independent
    // products. With R=2^52 and m=T.low*(-q^-1) mod R, T+m*q has a zero
    // low limb; the shifted result is T.high+(m*q).high plus that limb's
    // single carry. The Montgomery bound leaves at most one subtraction.
    const auto zero = _mm512_setzero_si512();
    const auto product_low = _mm512_madd52lo_epu64(zero, left, right);
    const auto product_high = _mm512_madd52hi_epu64(zero, left, right);
    const auto multiplier =
        _mm512_madd52lo_epu64(zero, product_low, negative_inverse);
    const auto correction_low =
        _mm512_madd52lo_epu64(zero, multiplier, modulus);
    const auto correction_high =
        _mm512_madd52hi_epu64(zero, multiplier, modulus);
    const auto carry = _mm512_srli_epi64(
        _mm512_add_epi64(product_low, correction_low), 52);
    auto reduced = _mm512_add_epi64(
        _mm512_add_epi64(product_high, correction_high), carry);
    const auto subtract = _mm512_cmp_epu64_mask(reduced, modulus, _MM_CMPINT_GE);
    reduced = _mm512_mask_sub_epi64(reduced, subtract, reduced, modulus);
    return reduced;
}

}  // namespace

Avx512IfmaBatch inverse_power_of_two_avx512_ifma(
    std::uint32_t exponent, const Avx512IfmaBatch& moduli) noexcept {
    Avx512IfmaBatch negative_inverses{};
    Avx512IfmaBatch ones{};
    Avx512IfmaBatch inverse_twos{};
    for (std::size_t lane = 0U; lane < avx512_ifma_lane_count; ++lane) {
        const auto modulus = moduli[lane];
        std::uint64_t inverse = 1U;
        for (unsigned int round = 0U; round < 6U; ++round) {
            inverse *= 2U - modulus * inverse;
        }
        negative_inverses[lane] = (0U - inverse) & radix_mask;
        ones[lane] = radix % modulus;
        inverse_twos[lane] = (ones[lane] & 1U) == 0U
            ? ones[lane] / 2U
            : (ones[lane] + modulus) / 2U;
    }

    const auto modulus = _mm512_loadu_si512(moduli.data());
    const auto negative_inverse = _mm512_loadu_si512(negative_inverses.data());
    auto result = _mm512_loadu_si512(ones.data());
    auto base = _mm512_loadu_si512(inverse_twos.data());
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = montgomery_multiply(result, base, modulus, negative_inverse);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = montgomery_multiply(base, base, modulus, negative_inverse);
        }
    }
    result = montgomery_multiply(
        result, _mm512_set1_epi64(1), modulus, negative_inverse);

    Avx512IfmaBatch standard{};
    _mm512_storeu_si512(standard.data(), result);
    return standard;
}

}  // namespace primeforge::discovery::detail
