// SPDX-License-Identifier: Apache-2.0

#include "primeforge/math/big_integer.hpp"

#include "primeforge/math/mul128.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace primeforge::math {
namespace {

[[nodiscard]] std::uint64_t add_mod(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::uint64_t modulus) noexcept {
    return left >= modulus - right ? left - (modulus - right) : left + right;
}

}  // namespace

BigInteger::BigInteger(const std::int64_t value) {
    if (value == 0) {
        return;
    }
    sign_ = value < 0 ? -1 : 1;
    auto magnitude = value < 0
                         ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                         : static_cast<std::uint64_t>(value);
    while (magnitude != 0U) {
        limbs_.push_back(static_cast<std::uint32_t>(magnitude % limb_base));
        magnitude /= limb_base;
    }
}

BigInteger BigInteger::from_decimal(const std::string_view decimal) {
    if (decimal.empty() || decimal == "-0" || decimal.front() == '+') {
        throw std::invalid_argument("non-canonical BigInteger decimal");
    }
    std::size_t offset = 0U;
    int sign = 1;
    if (decimal.front() == '-') {
        sign = -1;
        offset = 1U;
    }
    if (offset == decimal.size() ||
        (decimal.size() - offset > 1U && decimal[offset] == '0')) {
        throw std::invalid_argument("non-canonical BigInteger decimal");
    }
    for (std::size_t index = offset; index < decimal.size(); ++index) {
        const char character = decimal[index];
        if (character < '0' || character > '9') {
            throw std::invalid_argument("invalid BigInteger decimal digit");
        }
    }

    BigInteger result;
    for (std::size_t end = decimal.size(); end > offset;) {
        const auto start = end - offset > 9U ? end - 9U : offset;
        std::uint32_t limb{};
        const auto parsed = std::from_chars(
            decimal.data() + start, decimal.data() + end, limb);
        if (parsed.ec != std::errc{} || parsed.ptr != decimal.data() + end) {
            throw std::invalid_argument("invalid BigInteger decimal limb");
        }
        result.limbs_.push_back(limb);
        end = start;
    }
    result.sign_ = sign;
    result.normalize();
    return result;
}

std::string BigInteger::to_decimal() const {
    if (is_zero()) {
        return "0";
    }
    std::ostringstream output;
    if (sign_ < 0) {
        output << '-';
    }
    output << limbs_.back();
    for (auto index = limbs_.size() - 1U; index != 0U; --index) {
        output << std::setw(9) << std::setfill('0') << limbs_[index - 1U];
    }
    return output.str();
}

bool BigInteger::is_zero() const noexcept {
    return sign_ == 0;
}

bool BigInteger::is_negative() const noexcept {
    return sign_ < 0;
}

std::uint64_t BigInteger::modulo(const std::uint64_t modulus) const {
    if (modulus == 0U) {
        throw std::invalid_argument("BigInteger modulus must be nonzero");
    }
    std::uint64_t remainder = 0U;
    for (auto index = limbs_.size(); index != 0U; --index) {
        remainder = multiply_mod(remainder, limb_base, modulus);
        remainder = add_mod(remainder, limbs_[index - 1U] % modulus, modulus);
    }
    if (sign_ < 0 && remainder != 0U) {
        remainder = modulus - remainder;
    }
    return remainder;
}

std::optional<std::uint64_t> BigInteger::to_uint64_absolute() const noexcept {
    std::uint64_t value = 0U;
    for (auto index = limbs_.size(); index != 0U; --index) {
        if (value > (std::numeric_limits<std::uint64_t>::max() - limbs_[index - 1U]) /
                        limb_base) {
            return std::nullopt;
        }
        value = value * limb_base + limbs_[index - 1U];
    }
    return value;
}

BigInteger BigInteger::operator-() const {
    auto result = *this;
    result.sign_ = -result.sign_;
    return result;
}

BigInteger& BigInteger::operator+=(const BigInteger& other) {
    if (other.sign_ == 0) {
        return *this;
    }
    if (sign_ == 0) {
        *this = other;
        return *this;
    }
    if (sign_ == other.sign_) {
        auto result = add_absolute(*this, other);
        result.sign_ = sign_;
        *this = std::move(result);
        return *this;
    }
    const auto comparison = compare_absolute(*this, other);
    if (comparison == 0) {
        sign_ = 0;
        limbs_.clear();
    } else if (comparison > 0) {
        auto result = subtract_absolute(*this, other);
        result.sign_ = sign_;
        *this = std::move(result);
    } else {
        auto result = subtract_absolute(other, *this);
        result.sign_ = other.sign_;
        *this = std::move(result);
    }
    return *this;
}

BigInteger& BigInteger::operator-=(const BigInteger& other) {
    return *this += -other;
}

BigInteger& BigInteger::operator*=(const BigInteger& other) {
    if (is_zero() || other.is_zero()) {
        sign_ = 0;
        limbs_.clear();
        return *this;
    }
    std::vector<std::uint64_t> accumulation(limbs_.size() + other.limbs_.size(), 0U);
    for (std::size_t left = 0U; left < limbs_.size(); ++left) {
        std::uint64_t carry = 0U;
        for (std::size_t right = 0U; right < other.limbs_.size(); ++right) {
            const auto index = left + right;
            const auto product = static_cast<std::uint64_t>(limbs_[left]) * other.limbs_[right];
            const auto value = accumulation[index] + product + carry;
            accumulation[index] = value % limb_base;
            carry = value / limb_base;
        }
        auto index = left + other.limbs_.size();
        while (carry != 0U) {
            if (index == accumulation.size()) {
                accumulation.push_back(0U);
            }
            const auto value = accumulation[index] + carry;
            accumulation[index] = value % limb_base;
            carry = value / limb_base;
            ++index;
        }
    }
    limbs_.resize(accumulation.size());
    std::transform(accumulation.begin(), accumulation.end(), limbs_.begin(), [](const std::uint64_t value) {
        return static_cast<std::uint32_t>(value);
    });
    sign_ *= other.sign_;
    normalize();
    return *this;
}

std::strong_ordering operator<=> (
    const BigInteger& left, const BigInteger& right) noexcept {
    if (left.sign_ != right.sign_) {
        return left.sign_ <=> right.sign_;
    }
    if (left.sign_ == 0) {
        return std::strong_ordering::equal;
    }
    const auto comparison = BigInteger::compare_absolute(left, right);
    if (comparison == 0) {
        return std::strong_ordering::equal;
    }
    const auto absolute_order = comparison < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
    return left.sign_ > 0 ? absolute_order : 0 <=> comparison;
}

void BigInteger::normalize() noexcept {
    while (!limbs_.empty() && limbs_.back() == 0U) {
        limbs_.pop_back();
    }
    if (limbs_.empty()) {
        sign_ = 0;
    }
}

int BigInteger::compare_absolute(const BigInteger& left, const BigInteger& right) noexcept {
    if (left.limbs_.size() != right.limbs_.size()) {
        return left.limbs_.size() < right.limbs_.size() ? -1 : 1;
    }
    for (auto index = left.limbs_.size(); index != 0U; --index) {
        if (left.limbs_[index - 1U] != right.limbs_[index - 1U]) {
            return left.limbs_[index - 1U] < right.limbs_[index - 1U] ? -1 : 1;
        }
    }
    return 0;
}

BigInteger BigInteger::add_absolute(const BigInteger& left, const BigInteger& right) {
    BigInteger result;
    const auto size = std::max(left.limbs_.size(), right.limbs_.size());
    result.limbs_.resize(size);
    std::uint64_t carry = 0U;
    for (std::size_t index = 0U; index < size; ++index) {
        const auto left_limb = index < left.limbs_.size() ? left.limbs_[index] : 0U;
        const auto right_limb = index < right.limbs_.size() ? right.limbs_[index] : 0U;
        const auto value = static_cast<std::uint64_t>(left_limb) + right_limb + carry;
        result.limbs_[index] = static_cast<std::uint32_t>(value % limb_base);
        carry = value / limb_base;
    }
    if (carry != 0U) {
        result.limbs_.push_back(static_cast<std::uint32_t>(carry));
    }
    result.sign_ = result.limbs_.empty() ? 0 : 1;
    return result;
}

BigInteger BigInteger::subtract_absolute(
    const BigInteger& larger, const BigInteger& smaller) {
    BigInteger result;
    result.limbs_.resize(larger.limbs_.size());
    std::int64_t borrow = 0;
    for (std::size_t index = 0U; index < larger.limbs_.size(); ++index) {
        const auto smaller_limb = index < smaller.limbs_.size() ? smaller.limbs_[index] : 0U;
        auto value = static_cast<std::int64_t>(larger.limbs_[index]) - smaller_limb - borrow;
        if (value < 0) {
            value += limb_base;
            borrow = 1;
        } else {
            borrow = 0;
        }
        result.limbs_[index] = static_cast<std::uint32_t>(value);
    }
    result.sign_ = 1;
    result.normalize();
    return result;
}

BigInteger power(BigInteger base, std::uint64_t exponent) {
    BigInteger result{1};
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result *= base;
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base *= base;
        }
    }
    return result;
}

}  // namespace primeforge::math
