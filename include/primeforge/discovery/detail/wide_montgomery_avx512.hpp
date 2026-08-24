// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace primeforge::discovery::detail {

inline constexpr std::size_t avx512_ifma_lane_count = 8U;
inline constexpr std::uint64_t avx512_ifma_modulus_limit = 1ULL << 52U;
using Avx512IfmaBatch = std::array<std::uint64_t, avx512_ifma_lane_count>;

// Preconditions: every modulus is odd and strictly below 2^52, and the host
// has AVX-512F plus AVX-512IFMA enabled by the operating system.
[[nodiscard]] Avx512IfmaBatch inverse_power_of_two_avx512_ifma(
    std::uint32_t exponent, const Avx512IfmaBatch& moduli) noexcept;

}  // namespace primeforge::discovery::detail
