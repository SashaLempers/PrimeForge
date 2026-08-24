// SPDX-License-Identifier: Apache-2.0

#include "primeforge/discovery/proth_sieve.hpp"
#include "primeforge/math/mul128.hpp"

#include <primesieve.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#if defined(__SIZEOF_INT128__)
__extension__ typedef unsigned __int128 PrimeForgeSieveNativeUInt128;
#endif

namespace primeforge::discovery {
namespace {

constexpr std::uint64_t maximum_supported_sieve_prime =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

[[nodiscard]] inline math::UInt128Product multiply_full_inline(
    const std::uint64_t left, const std::uint64_t right) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    std::uint64_t high = 0U;
    const auto low = _umul128(left, right, &high);
    return {low, high};
#elif defined(__SIZEOF_INT128__)
    const auto product = static_cast<PrimeForgeSieveNativeUInt128>(left) *
                         static_cast<PrimeForgeSieveNativeUInt128>(right);
    return {static_cast<std::uint64_t>(product),
            static_cast<std::uint64_t>(product >> 64U)};
#else
    return math::multiply_full(left, right);
#endif
}

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

    void merge(const CandidateBitset& other) {
        if (bit_count_ != other.bit_count_) {
            throw std::invalid_argument("cannot merge candidate bitsets of different sizes");
        }
        for (std::size_t index = 0U; index < words_.size(); ++index) {
            words_[index] |= other.words_[index];
        }
    }

private:
    std::size_t bit_count_{};
    std::vector<std::uint64_t> words_;
};

[[nodiscard]] std::uint64_t modular_power_u32(
    std::uint64_t base, std::uint32_t exponent, const std::uint64_t modulus) noexcept {
    std::uint64_t result = 1U;
    base %= modulus;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) result = (result * base) % modulus;
        exponent >>= 1U;
        if (exponent != 0U) base = (base * base) % modulus;
    }
    return result;
}

class Montgomery64 {
public:
    explicit Montgomery64(const std::uint64_t modulus) : modulus_(modulus) {
        // Newton iteration doubles the number of correct low bits each time.
        // Six rounds recover the inverse of an odd modulus modulo 2^64.
        std::uint64_t inverse = 1U;
        for (unsigned int round = 0U; round < 6U; ++round) {
            inverse *= 2U - modulus_ * inverse;
        }
        negative_inverse_ = 0U - inverse;

        // Unsigned wrap computes 2^64 - modulus, which has the same remainder
        // as R=2^64. The signed-64 cap also guarantees REDC < 2^64.
        one_ = (0U - modulus_) % modulus_;
    }

    [[nodiscard]] std::uint64_t one() const noexcept { return one_; }

    [[nodiscard]] std::uint64_t inverse_two() const noexcept {
        return (one_ & 1U) == 0U ? one_ / 2U : (one_ + modulus_) / 2U;
    }

    [[nodiscard]] std::uint64_t multiply(
        const std::uint64_t left, const std::uint64_t right) const noexcept {
        const auto product = multiply_full_inline(left, right);
        const auto multiplier = product.low * negative_inverse_;
        const auto correction = multiply_full_inline(multiplier, modulus_);

        // correction.low is -product.low modulo 2^64. A nonzero low word
        // therefore contributes exactly one carry to the high-word sum.
        auto reduced = product.high + correction.high + (product.low != 0U ? 1U : 0U);
        if (reduced >= modulus_) reduced -= modulus_;
        return reduced;
    }

    [[nodiscard]] std::uint64_t to_standard(const std::uint64_t value) const noexcept {
        return multiply(value, 1U);
    }

private:
    std::uint64_t modulus_{};
    std::uint64_t negative_inverse_{};
    std::uint64_t one_{};
};

[[nodiscard]] std::uint64_t modular_inverse_power_of_two(
    std::uint32_t exponent, const std::uint64_t modulus) noexcept {
    if (modulus <= std::numeric_limits<std::uint32_t>::max()) {
        return modular_power_u32((modulus + 1U) / 2U, exponent, modulus);
    }

    const Montgomery64 ring{modulus};
    auto result = ring.one();
    auto base = ring.inverse_two();
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) result = ring.multiply(result, base);
        exponent >>= 1U;
        if (exponent != 0U) base = ring.multiply(base, base);
    }
    return ring.to_standard(result);
}

constexpr std::size_t wide_inverse_batch_size = 4U;
using WideInverseBatch = std::array<std::uint64_t, wide_inverse_batch_size>;
using WideMontgomeryBatch = std::array<Montgomery64, wide_inverse_batch_size>;
using WideBatchIndices = std::make_index_sequence<wide_inverse_batch_size>;

template <std::size_t... Lanes>
[[nodiscard]] WideMontgomeryBatch make_wide_rings(
    const WideInverseBatch& moduli, std::index_sequence<Lanes...>) noexcept {
    return {Montgomery64{moduli[Lanes]}...};
}

template <std::size_t... Lanes>
[[nodiscard]] WideInverseBatch wide_ones(
    const WideMontgomeryBatch& rings, std::index_sequence<Lanes...>) noexcept {
    return {rings[Lanes].one()...};
}

template <std::size_t... Lanes>
[[nodiscard]] WideInverseBatch wide_inverse_twos(
    const WideMontgomeryBatch& rings, std::index_sequence<Lanes...>) noexcept {
    return {rings[Lanes].inverse_two()...};
}

template <std::size_t... Lanes>
void wide_multiply(
    const WideMontgomeryBatch& rings, WideInverseBatch& destination,
    const WideInverseBatch& right, std::index_sequence<Lanes...>) noexcept {
    ((destination[Lanes] = rings[Lanes].multiply(destination[Lanes], right[Lanes])), ...);
}

template <std::size_t... Lanes>
[[nodiscard]] WideInverseBatch wide_to_standard(
    const WideMontgomeryBatch& rings, const WideInverseBatch& values,
    std::index_sequence<Lanes...>) noexcept {
    return {rings[Lanes].to_standard(values[Lanes])...};
}

[[nodiscard]] WideInverseBatch modular_inverse_power_of_two_wide_batch(
    std::uint32_t exponent, const WideInverseBatch& moduli) noexcept {
    const auto rings = make_wide_rings(moduli, WideBatchIndices{});
    auto result = wide_ones(rings, WideBatchIndices{});
    auto base = wide_inverse_twos(rings, WideBatchIndices{});
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            wide_multiply(rings, result, base, WideBatchIndices{});
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            wide_multiply(rings, base, base, WideBatchIndices{});
        }
    }
    return wide_to_standard(rings, result, WideBatchIndices{});
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
    if (config.maximum_prime < 3U ||
        config.maximum_prime > maximum_supported_sieve_prime) {
        throw std::invalid_argument(
            "Proth discovery sieve bound must be in [3, 9223372036854775807]");
    }
    if (config.minimum_prime < 3U || config.minimum_prime > config.maximum_prime) {
        throw std::invalid_argument(
            "Proth discovery sieve start must be in [3, maximum_prime]");
    }
    if (config.thread_count < 1U || config.thread_count > 64U) {
        throw std::invalid_argument("Proth discovery sieve threads must be in [1, 64]");
    }
}

struct WorkerResult {
    CandidateBitset eliminated;
    std::uint64_t primes_applied{};
};

}  // namespace

ProthSieveResult sieve_proth_candidates(const ProthSieveConfig& config) {
    validate(config);
    const auto candidate_count =
        (static_cast<std::uint64_t>(config.k_stop) - config.k_start) / 2U + 1U;
    if (candidate_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::length_error("Proth discovery candidate set does not fit in memory");
    }

    const auto candidate_size = static_cast<std::size_t>(candidate_count);
    const auto window_width = static_cast<std::uint64_t>(config.k_stop) - config.k_start;

    const auto scan_interval = [&](const std::uint64_t interval_start,
                                   const std::uint64_t interval_stop) {
        WorkerResult worker{CandidateBitset{candidate_size}, 0U};
        const auto apply_prime = [&](const std::uint64_t prime, const std::uint64_t inverse) {
            const auto residue = (prime - inverse) % prime;
            const auto start_residue = static_cast<std::uint64_t>(config.k_start) % prime;
            const auto delta = (residue + prime - start_residue) % prime;
            std::uint64_t first = static_cast<std::uint64_t>(config.k_start) + delta;
            if ((first & 1U) == 0U) first += prime;

            const auto mark = [&](const std::uint64_t k) {
                const auto index = static_cast<std::size_t>((k - config.k_start) / 2U);
                static_cast<void>(worker.eliminated.mark(index));
            };

            // Once q is wider than the k window, at most one candidate can match.
            if (prime > window_width) {
                if (first <= config.k_stop) mark(first);
                return;
            }

            const auto step = 2U * prime;
            for (auto k = first; k <= config.k_stop; k += step) mark(k);
        };
        const auto apply_wide_prime = [&](const std::uint64_t prime,
                                          const std::uint64_t inverse) {
            // Every wide prime is greater than the complete supported k domain.
            // Its single residue can therefore hit at most one odd candidate.
            const auto residue = prime - inverse;
            if (residue < config.k_start || residue > config.k_stop ||
                (residue & 1U) == 0U) {
                return;
            }
            const auto index = static_cast<std::size_t>(
                (residue - static_cast<std::uint64_t>(config.k_start)) / 2U);
            static_cast<void>(worker.eliminated.mark(index));
        };
        WideInverseBatch wide_primes{};
        std::size_t wide_count = 0U;
        const auto flush_wide_primes = [&]() {
            if (wide_count == wide_inverse_batch_size) {
                const auto inverses = modular_inverse_power_of_two_wide_batch(
                    config.exponent, wide_primes);
                for (std::size_t lane = 0U; lane < wide_inverse_batch_size; ++lane) {
                    apply_wide_prime(wide_primes[lane], inverses[lane]);
                }
            } else {
                for (std::size_t lane = 0U; lane < wide_count; ++lane) {
                    apply_wide_prime(wide_primes[lane], modular_inverse_power_of_two(
                        config.exponent, wide_primes[lane]));
                }
            }
            wide_count = 0U;
        };

        primesieve::iterator primes{interval_start, interval_stop};
        for (;;) {
            const auto prime = primes.next_prime();
            if (prime > interval_stop) break;
            ++worker.primes_applied;

            // (q + 1) / 2 is 2^-1 modulo every odd prime q. Raising it to n
            // computes 2^-n directly and removes the former second powmod.
            if (prime <= std::numeric_limits<std::uint32_t>::max()) {
                apply_prime(prime, modular_inverse_power_of_two(config.exponent, prime));
                continue;
            }

            wide_primes[wide_count++] = prime;
            if (wide_count == wide_inverse_batch_size) flush_wide_primes();
        }
        flush_wide_primes();
        return worker;
    };

    const auto prime_span = config.maximum_prime - config.minimum_prime + 1U;
    const auto worker_count = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(config.thread_count, prime_span));
    const auto base_span = prime_span / worker_count;
    const auto extra_intervals = prime_span % worker_count;
    std::vector<std::future<WorkerResult>> futures;
    futures.reserve(worker_count);
    for (std::uint32_t worker = 0U; worker < worker_count; ++worker) {
        const auto interval_start =
            config.minimum_prime + base_span * worker +
            std::min<std::uint64_t>(worker, extra_intervals);
        const auto interval_size = base_span + (worker < extra_intervals ? 1U : 0U);
        const auto interval_stop = interval_start + interval_size - 1U;
        futures.push_back(std::async(
            std::launch::async, scan_interval, interval_start, interval_stop));
    }

    CandidateBitset eliminated(candidate_size);
    std::uint64_t primes_applied = 0U;
    for (auto& future : futures) {
        auto worker = future.get();
        eliminated.merge(worker.eliminated);
        primes_applied += worker.primes_applied;
    }
    const auto eliminated_count = eliminated.marked_count();

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
