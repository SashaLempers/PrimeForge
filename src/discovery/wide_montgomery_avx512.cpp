// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/detail/wide_montgomery_avx512.hpp"

#include <immintrin.h>

namespace primeforge::discovery::detail {
namespace {

constexpr std::size_t lanes_per_vector = 8U;
constexpr std::uint64_t radix_mask = avx512_ifma_radix - 1U;

struct VectorPair {
    __m512i first;
    __m512i second;
    __m512i third;
    __m512i fourth;
};

[[nodiscard]] VectorPair load_pair(const Avx512IfmaBatch& values) noexcept {
    return {
        _mm512_loadu_si512(values.data()),
        _mm512_loadu_si512(values.data() + lanes_per_vector),
        _mm512_loadu_si512(values.data() + 2U * lanes_per_vector),
        _mm512_loadu_si512(values.data() + 3U * lanes_per_vector),
    };
}

void store_pair(Avx512IfmaBatch& destination, const VectorPair values) noexcept {
    _mm512_storeu_si512(destination.data(), values.first);
    _mm512_storeu_si512(destination.data() + lanes_per_vector, values.second);
    _mm512_storeu_si512(destination.data() + 2U * lanes_per_vector, values.third);
    _mm512_storeu_si512(destination.data() + 3U * lanes_per_vector, values.fourth);
}

[[nodiscard]] VectorPair montgomery_multiply(
    const VectorPair left, const VectorPair right, const VectorPair modulus,
    const VectorPair negative_inverse) noexcept {
    // IFMA exposes the low and high 52-bit limbs of eight independent
    // products. With R=2^52 and m=T.low*(-q^-1) mod R, T+m*q has a zero
    // low limb; the shifted result is T.high+(m*q).high plus that limb's
    // single carry. The Montgomery bound leaves at most one subtraction.
    const auto zero = _mm512_setzero_si512();
    const VectorPair product_low{
        _mm512_madd52lo_epu64(zero, left.first, right.first),
        _mm512_madd52lo_epu64(zero, left.second, right.second),
        _mm512_madd52lo_epu64(zero, left.third, right.third),
        _mm512_madd52lo_epu64(zero, left.fourth, right.fourth)};
    const VectorPair product_high{
        _mm512_madd52hi_epu64(zero, left.first, right.first),
        _mm512_madd52hi_epu64(zero, left.second, right.second),
        _mm512_madd52hi_epu64(zero, left.third, right.third),
        _mm512_madd52hi_epu64(zero, left.fourth, right.fourth)};
    const VectorPair multiplier{
        _mm512_madd52lo_epu64(zero, product_low.first, negative_inverse.first),
        _mm512_madd52lo_epu64(zero, product_low.second, negative_inverse.second),
        _mm512_madd52lo_epu64(zero, product_low.third, negative_inverse.third),
        _mm512_madd52lo_epu64(zero, product_low.fourth, negative_inverse.fourth)};
    const VectorPair correction_low{
        _mm512_madd52lo_epu64(zero, multiplier.first, modulus.first),
        _mm512_madd52lo_epu64(zero, multiplier.second, modulus.second),
        _mm512_madd52lo_epu64(zero, multiplier.third, modulus.third),
        _mm512_madd52lo_epu64(zero, multiplier.fourth, modulus.fourth)};
    const VectorPair correction_high{
        _mm512_madd52hi_epu64(zero, multiplier.first, modulus.first),
        _mm512_madd52hi_epu64(zero, multiplier.second, modulus.second),
        _mm512_madd52hi_epu64(zero, multiplier.third, modulus.third),
        _mm512_madd52hi_epu64(zero, multiplier.fourth, modulus.fourth)};
    const VectorPair carry{
        _mm512_srli_epi64(
            _mm512_add_epi64(product_low.first, correction_low.first), 52),
        _mm512_srli_epi64(
            _mm512_add_epi64(product_low.second, correction_low.second), 52),
        _mm512_srli_epi64(
            _mm512_add_epi64(product_low.third, correction_low.third), 52),
        _mm512_srli_epi64(
            _mm512_add_epi64(product_low.fourth, correction_low.fourth), 52)};
    VectorPair reduced{
        _mm512_add_epi64(
            _mm512_add_epi64(product_high.first, correction_high.first), carry.first),
        _mm512_add_epi64(
            _mm512_add_epi64(product_high.second, correction_high.second), carry.second),
        _mm512_add_epi64(
            _mm512_add_epi64(product_high.third, correction_high.third), carry.third),
        _mm512_add_epi64(
            _mm512_add_epi64(product_high.fourth, correction_high.fourth), carry.fourth)};
    const auto subtract_first =
        _mm512_cmp_epu64_mask(reduced.first, modulus.first, _MM_CMPINT_GE);
    const auto subtract_second =
        _mm512_cmp_epu64_mask(reduced.second, modulus.second, _MM_CMPINT_GE);
    const auto subtract_third =
        _mm512_cmp_epu64_mask(reduced.third, modulus.third, _MM_CMPINT_GE);
    const auto subtract_fourth =
        _mm512_cmp_epu64_mask(reduced.fourth, modulus.fourth, _MM_CMPINT_GE);
    reduced.first = _mm512_mask_sub_epi64(
        reduced.first, subtract_first, reduced.first, modulus.first);
    reduced.second = _mm512_mask_sub_epi64(
        reduced.second, subtract_second, reduced.second, modulus.second);
    reduced.third = _mm512_mask_sub_epi64(
        reduced.third, subtract_third, reduced.third, modulus.third);
    reduced.fourth = _mm512_mask_sub_epi64(
        reduced.fourth, subtract_fourth, reduced.fourth, modulus.fourth);
    return reduced;
}

[[nodiscard]] VectorPair negative_inverses(const VectorPair modulus) noexcept {
    const auto zero = _mm512_setzero_si512();
    const auto two = _mm512_set1_epi64(2);
    VectorPair inverse{
        _mm512_set1_epi64(1), _mm512_set1_epi64(1),
        _mm512_set1_epi64(1), _mm512_set1_epi64(1)};
    for (unsigned int round = 0U; round < 6U; ++round) {
        const VectorPair product{
            _mm512_madd52lo_epu64(zero, modulus.first, inverse.first),
            _mm512_madd52lo_epu64(zero, modulus.second, inverse.second),
            _mm512_madd52lo_epu64(zero, modulus.third, inverse.third),
            _mm512_madd52lo_epu64(zero, modulus.fourth, inverse.fourth)};
        const VectorPair correction{
            _mm512_sub_epi64(two, product.first),
            _mm512_sub_epi64(two, product.second),
            _mm512_sub_epi64(two, product.third),
            _mm512_sub_epi64(two, product.fourth)};
        inverse.first =
            _mm512_madd52lo_epu64(zero, inverse.first, correction.first);
        inverse.second =
            _mm512_madd52lo_epu64(zero, inverse.second, correction.second);
        inverse.third =
            _mm512_madd52lo_epu64(zero, inverse.third, correction.third);
        inverse.fourth =
            _mm512_madd52lo_epu64(zero, inverse.fourth, correction.fourth);
    }
    const auto mask = _mm512_set1_epi64(static_cast<std::int64_t>(radix_mask));
    return {
        _mm512_and_si512(_mm512_sub_epi64(zero, inverse.first), mask),
        _mm512_and_si512(_mm512_sub_epi64(zero, inverse.second), mask),
        _mm512_and_si512(_mm512_sub_epi64(zero, inverse.third), mask),
        _mm512_and_si512(_mm512_sub_epi64(zero, inverse.fourth), mask),
    };
}

[[nodiscard]] VectorPair inverse_twos(
    const VectorPair ones, const VectorPair modulus) noexcept {
    const auto zero = _mm512_setzero_si512();
    const auto one = _mm512_set1_epi64(1);
    const auto odd_first = _mm512_cmpneq_epi64_mask(
        _mm512_and_si512(ones.first, one), zero);
    const auto odd_second = _mm512_cmpneq_epi64_mask(
        _mm512_and_si512(ones.second, one), zero);
    const auto odd_third = _mm512_cmpneq_epi64_mask(
        _mm512_and_si512(ones.third, one), zero);
    const auto odd_fourth = _mm512_cmpneq_epi64_mask(
        _mm512_and_si512(ones.fourth, one), zero);
    return {
        _mm512_srli_epi64(_mm512_mask_add_epi64(
            ones.first, odd_first, ones.first, modulus.first), 1),
        _mm512_srli_epi64(_mm512_mask_add_epi64(
            ones.second, odd_second, ones.second, modulus.second), 1),
        _mm512_srli_epi64(_mm512_mask_add_epi64(
            ones.third, odd_third, ones.third, modulus.third), 1),
        _mm512_srli_epi64(_mm512_mask_add_epi64(
            ones.fourth, odd_fourth, ones.fourth, modulus.fourth), 1),
    };
}

}  // namespace

Avx512IfmaBatch inverse_power_of_two_avx512_ifma(
    std::uint32_t exponent, const Avx512IfmaBatch& moduli,
    const Avx512IfmaBatch& montgomery_ones) noexcept {
    const auto modulus = load_pair(moduli);
    const auto negative_inverse = negative_inverses(modulus);
    auto result = load_pair(montgomery_ones);
    auto base = inverse_twos(result, modulus);
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = montgomery_multiply(result, base, modulus, negative_inverse);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = montgomery_multiply(base, base, modulus, negative_inverse);
        }
    }
    const VectorPair standard_one{
        _mm512_set1_epi64(1), _mm512_set1_epi64(1),
        _mm512_set1_epi64(1), _mm512_set1_epi64(1)};
    result = montgomery_multiply(result, standard_one, modulus, negative_inverse);

    Avx512IfmaBatch standard{};
    store_pair(standard, result);
    return standard;
}

}  // namespace primeforge::discovery::detail
