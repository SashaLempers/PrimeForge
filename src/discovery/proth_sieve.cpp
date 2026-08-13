// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/proth_sieve.hpp"

#include <primesieve.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace primeforge::discovery {
namespace {

class CandidateBitset {
public:
    explicit CandidateBitset(const std::size_t bit_count)
        : bit_count_(bit_count), words_((bit_count + 63U) / 64U, 0U) {}

    [[nodiscard]] bool mark(const std::size_t index) noexcept {
        const auto mask = std::uint64_t{1U} << (index % 64U);
        auto& word = words_[index / 64U];
        const auto was_clear = (word & mask) == 0U;
        word |= mask;
        return was_clear;
    }

    [[nodiscard]] bool test(const std::size_t index) const noexcept {
        return (words_[index / 64U] & (std::uint64_t{1U} << (index % 64U))) != 0U;
    }

    [[nodiscard]] std::size_t size() const noexcept { return bit_count_; }

    [[nodiscard]] std::uint64_t marked_count() const noexcept {
        std::uint64_t result = 0U;
        for (const auto word : words_) result += std::popcount(word);
        return result;
    }

private:
    std::size_t bit_count_{};
    std::vector<std::uint64_t> words_;
};

[[nodiscard]] std::uint64_t modular_power(
    std::uint64_t base, std::uint64_t exponent, const std::uint64_t modulus) noexcept {
    std::uint64_t result = 1U;
    base %= modulus;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) result = (result * base) % modulus;
        exponent >>= 1U;
        if (exponent != 0U) base = (base * base) % modulus;
    }
    return result;
}

void validate(const ProthSieveConfig& config) {
    if (config.k_start < 3U || config.k_start > config.k_stop ||
        (config.k_start & 1U) == 0U || (config.k_stop & 1U) == 0U) {
        throw std::invalid_argument("Proth discovery k bounds must be ordered odd integers >= 3");
    }
    if (config.k_stop > 99'999'999U) {
        throw std::invalid_argument("Proth discovery k bound exceeds the pinned engine limit");
    }
    if (config.exponent < 32U || config.exponent > 99'999'999U) {
        throw std::invalid_argument("Proth discovery exponent exceeds the pinned engine domain");
    }
    if (config.maximum_prime < 3U || config.maximum_prime > 4'000'000'000U) {
        throw std::invalid_argument("Proth discovery sieve bound must be in [3, 4000000000]");
    }
}

}  // namespace

ProthSieveResult sieve_proth_candidates(const ProthSieveConfig& config) {
    validate(config);
    const auto candidate_count =
        (static_cast<std::uint64_t>(config.k_stop) - config.k_start) / 2U + 1U;
    if (candidate_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::length_error("Proth discovery candidate set does not fit in memory");
    }

    CandidateBitset eliminated(static_cast<std::size_t>(candidate_count));
    primesieve::iterator primes{3U, config.maximum_prime};
    std::uint64_t eliminated_count = 0U;
    std::uint64_t primes_applied = 0U;
    const auto window_width = static_cast<std::uint64_t>(config.k_stop) - config.k_start;

    for (;;) {
        const auto prime = primes.next_prime();
        if (prime > config.maximum_prime) break;
        ++primes_applied;

        // (q + 1) / 2 is 2^-1 modulo every odd prime q.  Raising it to n
        // computes 2^-n directly and removes the former second powmod.
        const auto inverse = modular_power((prime + 1U) / 2U, config.exponent, prime);
        const auto residue = (prime - inverse) % prime;
        const auto start_residue = static_cast<std::uint64_t>(config.k_start) % prime;
        const auto delta = (residue + prime - start_residue) % prime;
        std::uint64_t first = static_cast<std::uint64_t>(config.k_start) + delta;
        if ((first & 1U) == 0U) first += prime;

        const auto mark = [&](const std::uint64_t k) {
            const auto index = static_cast<std::size_t>((k - config.k_start) / 2U);
            if (eliminated.mark(index)) ++eliminated_count;
        };

        // Once q is wider than the k window, at most one candidate can match.
        if (prime > window_width) {
            if (first <= config.k_stop) mark(first);
            continue;
        }

        const auto step = 2U * prime;
        for (auto k = first; k <= config.k_stop; k += step) {
            mark(k);
        }
    }

    if (eliminated.marked_count() != eliminated_count) {
        throw std::logic_error("Proth sieve bitset population mismatch");
    }

    ProthSieveResult result;
    result.candidate_count = candidate_count;
    result.eliminated_count = eliminated_count;
    result.primes_applied = primes_applied;
    result.survivors.reserve(static_cast<std::size_t>(candidate_count - eliminated_count));
    for (std::size_t index = 0U; index < eliminated.size(); ++index) {
        if (!eliminated.test(index)) {
            result.survivors.push_back(
                config.k_start + static_cast<std::uint32_t>(2U * index));
        }
    }
    return result;
}

}  // namespace primeforge::discovery
