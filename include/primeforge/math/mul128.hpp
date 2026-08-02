// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace primeforge::math {

struct UInt128Product {
    std::uint64_t low{};
    std::uint64_t high{};

    [[nodiscard]] friend constexpr bool operator==(const UInt128Product&, const UInt128Product&) = default;
};

[[nodiscard]] UInt128Product multiply_full(std::uint64_t left, std::uint64_t right) noexcept;
[[nodiscard]] UInt128Product multiply_full_portable_reference(
    std::uint64_t left, std::uint64_t right) noexcept;
[[nodiscard]] std::uint64_t multiply_mod(
    std::uint64_t left, std::uint64_t right, std::uint64_t modulus);
[[nodiscard]] std::uint64_t multiply_mod_portable_reference(
    std::uint64_t left, std::uint64_t right, std::uint64_t modulus);

}  // namespace primeforge::math
