// SPDX-License-Identifier: Apache-2.0

#include "primeforge/math/mul128.hpp"

#include <limits>
#include <stdexcept>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#if defined(__SIZEOF_INT128__)
__extension__ typedef unsigned __int128 PrimeForgeNativeUInt128;
#endif

namespace primeforge::math {
namespace {

[[nodiscard]] constexpr std::uint64_t add_mod(
    const std::uint64_t left, const std::uint64_t right, const std::uint64_t modulus) noexcept {
    return left >= modulus - right ? left - (modulus - right) : left + right;
}

}  // namespace

UInt128Product multiply_full_portable_reference(
    const std::uint64_t left, const std::uint64_t right) noexcept {
    constexpr std::uint64_t mask = 0xffff'ffffULL;
    const auto left_low = left & mask;
    const auto left_high = left >> 32U;
    const auto right_low = right & mask;
    const auto right_high = right >> 32U;

    const auto product_low = left_low * right_low;
    const auto product_middle_a = left_high * right_low;
    const auto product_middle_b = left_low * right_high;
    const auto product_high = left_high * right_high;

    const auto carry = (product_low >> 32U) + (product_middle_a & mask) + (product_middle_b & mask);
    return {
        (product_low & mask) | (carry << 32U),
        product_high + (product_middle_a >> 32U) + (product_middle_b >> 32U) + (carry >> 32U),
    };
}

UInt128Product multiply_full(const std::uint64_t left, const std::uint64_t right) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    std::uint64_t high = 0;
    const auto low = _umul128(left, right, &high);
    return {low, high};
#elif defined(__SIZEOF_INT128__)
    const auto product = static_cast<PrimeForgeNativeUInt128>(left) *
                         static_cast<PrimeForgeNativeUInt128>(right);
    return {static_cast<std::uint64_t>(product), static_cast<std::uint64_t>(product >> 64U)};
#else
    return multiply_full_portable_reference(left, right);
#endif
}

std::uint64_t multiply_mod_portable_reference(
    std::uint64_t left, std::uint64_t right, const std::uint64_t modulus) {
    if (modulus == 0) {
        throw std::invalid_argument("modulus must be nonzero");
    }
    left %= modulus;
    right %= modulus;
    std::uint64_t result = 0;
    while (right != 0) {
        if ((right & 1U) != 0U) {
            result = add_mod(result, left, modulus);
        }
        right >>= 1U;
        if (right != 0) {
            left = add_mod(left, left, modulus);
        }
    }
    return result;
}

std::uint64_t multiply_mod(
    const std::uint64_t left, const std::uint64_t right, const std::uint64_t modulus) {
    if (modulus == 0) {
        throw std::invalid_argument("modulus must be nonzero");
    }
    const auto reduced_left = left % modulus;
    const auto reduced_right = right % modulus;
#if defined(_MSC_VER) && defined(_M_X64)
    std::uint64_t high = 0;
    const auto low = _umul128(reduced_left, reduced_right, &high);
    std::uint64_t remainder = 0;
    static_cast<void>(_udiv128(high, low, modulus, &remainder));
    return remainder;
#elif defined(__SIZEOF_INT128__)
    const auto product = static_cast<PrimeForgeNativeUInt128>(reduced_left) *
                         static_cast<PrimeForgeNativeUInt128>(reduced_right);
    return static_cast<std::uint64_t>(product % modulus);
#else
    return multiply_mod_portable_reference(reduced_left, reduced_right, modulus);
#endif
}

}  // namespace primeforge::math
