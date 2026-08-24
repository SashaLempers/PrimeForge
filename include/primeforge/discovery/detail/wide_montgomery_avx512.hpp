// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace primeforge::discovery::detail {

inline constexpr std::size_t avx512_ifma_lane_count = 16U;
inline constexpr std::uint64_t avx512_ifma_radix = 1ULL << 52U;
inline constexpr std::uint64_t avx512_ifma_modulus_limit = avx512_ifma_radix;
using Avx512IfmaBatch = std::array<std::uint64_t, avx512_ifma_lane_count>;

// Preconditions: every modulus is odd and strictly below 2^52, and the host
// has AVX-512F plus AVX-512IFMA enabled by the operating system. Each supplied
// Montgomery one is exactly 2^52 modulo its corresponding modulus.
[[nodiscard]] Avx512IfmaBatch inverse_power_of_two_avx512_ifma(
    std::uint32_t exponent, const Avx512IfmaBatch& moduli,
    const Avx512IfmaBatch& montgomery_ones) noexcept;

}  // namespace primeforge::discovery::detail
