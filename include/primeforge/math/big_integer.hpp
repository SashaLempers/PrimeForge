// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace primeforge::math {

class BigInteger {
public:
    BigInteger() = default;
    BigInteger(std::int64_t value);

    [[nodiscard]] static BigInteger from_decimal(std::string_view decimal);
    [[nodiscard]] std::string to_decimal() const;
    [[nodiscard]] bool is_zero() const noexcept;
    [[nodiscard]] bool is_negative() const noexcept;
    [[nodiscard]] std::uint64_t modulo(std::uint64_t modulus) const;
    [[nodiscard]] std::optional<std::uint64_t> to_uint64_absolute() const noexcept;

    [[nodiscard]] BigInteger operator-() const;
    BigInteger& operator+=(const BigInteger& other);
    BigInteger& operator-=(const BigInteger& other);
    BigInteger& operator*=(const BigInteger& other);

    [[nodiscard]] friend BigInteger operator+(BigInteger left, const BigInteger& right) {
        left += right;
        return left;
    }
    [[nodiscard]] friend BigInteger operator-(BigInteger left, const BigInteger& right) {
        left -= right;
        return left;
    }
    [[nodiscard]] friend BigInteger operator*(BigInteger left, const BigInteger& right) {
        left *= right;
        return left;
    }
    [[nodiscard]] friend bool operator==(const BigInteger&, const BigInteger&) = default;
    friend std::strong_ordering operator<=> (
        const BigInteger& left, const BigInteger& right) noexcept;

private:
    static constexpr std::uint32_t limb_base = 1'000'000'000U;
    int sign_{};
    std::vector<std::uint32_t> limbs_;

    void normalize() noexcept;
    [[nodiscard]] static int compare_absolute(
        const BigInteger& left, const BigInteger& right) noexcept;
    [[nodiscard]] static BigInteger add_absolute(
        const BigInteger& left, const BigInteger& right);
    [[nodiscard]] static BigInteger subtract_absolute(
        const BigInteger& larger, const BigInteger& smaller);
};

[[nodiscard]] BigInteger power(BigInteger base, std::uint64_t exponent);

}  // namespace primeforge::math
