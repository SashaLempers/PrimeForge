// SPDX-License-Identifier: Apache-2.0

#include "primeforge/sieve/sieve.hpp"

#include "primeforge/math/mul128.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace primeforge::sieve {
namespace {

constexpr std::uint64_t cache_line_bytes = 64;

struct BucketEntry {
    std::uint64_t value{};
    std::uint64_t prime{};
};

struct Bucket {
    std::vector<std::uint64_t> values;
    std::vector<std::uint64_t> primes;
    std::vector<BucketEntry> entries;
};

struct alignas(cache_line_bytes) SegmentOutput {
    std::vector<std::uint64_t> primes;
    static_assert(sizeof(primes) <= cache_line_bytes);
    std::array<std::byte, cache_line_bytes - sizeof(primes)> cache_line_padding{};
};

[[nodiscard]] std::uint64_t integer_square_root(const std::uint64_t value) noexcept {
    auto root = static_cast<std::uint64_t>(std::sqrt(static_cast<long double>(value)));
    while (root != std::numeric_limits<std::uint64_t>::max() &&
           root + 1U <= value / (root + 1U)) {
        ++root;
    }
    while (root != 0U && root > value / root) {
        --root;
    }
    return root;
}

[[nodiscard]] std::vector<std::uint64_t> base_primes_through(const std::uint64_t limit) {
    if (limit < 2U) {
        return {};
    }
    if (limit > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() - 1U)) {
        throw std::length_error("base-prime table exceeds addressable memory");
    }
    std::vector<std::uint8_t> composite(static_cast<std::size_t>(limit + 1U), 0U);
    for (std::uint64_t candidate = 2U; candidate <= limit / candidate; ++candidate) {
        if (composite[static_cast<std::size_t>(candidate)] != 0U) {
            continue;
        }
        for (auto multiple = candidate * candidate; multiple <= limit;) {
            composite[static_cast<std::size_t>(multiple)] = 1U;
            if (multiple > limit - candidate) {
                break;
            }
            multiple += candidate;
        }
    }
    std::vector<std::uint64_t> primes;
    for (std::uint64_t value = 2U; value <= limit; ++value) {
        if (composite[static_cast<std::size_t>(value)] == 0U) {
            primes.push_back(value);
        }
    }
    return primes;
}

[[nodiscard]] bool is_wheel_candidate(const std::uint64_t value) noexcept {
    if (value == 2U || value == 3U || value == 5U) {
        return true;
    }
    const auto residue = value % 30U;
    return residue == 1U || residue == 7U || residue == 11U || residue == 13U ||
           residue == 17U || residue == 19U || residue == 23U || residue == 29U;
}

[[nodiscard]] std::uint64_t first_odd_multiple(
    const std::uint64_t prime, const std::uint64_t low, const std::uint64_t high) noexcept {
    auto quotient = low / prime;
    if (low % prime != 0U) {
        if (quotient == std::numeric_limits<std::uint64_t>::max()) {
            return high;
        }
        ++quotient;
    }
    if (quotient > std::numeric_limits<std::uint64_t>::max() / prime) {
        return high;
    }
    auto first = quotient * prime;
    if (prime <= std::numeric_limits<std::uint64_t>::max() / prime) {
        first = std::max(first, prime * prime);
    }
    if ((first & 1U) == 0U) {
        if (first > std::numeric_limits<std::uint64_t>::max() - prime) {
            return high;
        }
        first += prime;
    }
    return first < high ? first : high;
}

void prefetch_read(const void* address) noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_prefetch(static_cast<const char*>(address), _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(address, 0, 3);
#else
    static_cast<void>(address);
#endif
}

class CompositeBits {
public:
    CompositeBits(const std::size_t size, const bool packed)
        : size_(size), packed_(packed),
          words_(packed ? (size + 63U) / 64U : 0U, 0U),
          bytes_(packed ? 0U : size, 0U) {}

    void set(const std::size_t index) noexcept {
        if (packed_) {
            words_[index / 64U] |= std::uint64_t{1U} << (index % 64U);
        } else {
            bytes_[index] = 1U;
        }
    }

    [[nodiscard]] bool test(const std::size_t index) const noexcept {
        if (packed_) {
            return (words_[index / 64U] & (std::uint64_t{1U} << (index % 64U))) != 0U;
        }
        return bytes_[index] != 0U;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::size_t size_{};
    bool packed_{};
    std::vector<std::uint64_t> words_;
    std::vector<std::uint8_t> bytes_;
};

[[nodiscard]] std::uint64_t modular_power(
    std::uint64_t base, std::uint64_t exponent, const std::uint64_t modulus) noexcept {
    std::uint64_t result = 1U;
    base %= modulus;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result = math::multiply_mod(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0U) {
            base = math::multiply_mod(base, base, modulus);
        }
    }
    return result;
}

}  // namespace

SieveResult generate_primes_reference(const std::uint64_t begin, const std::uint64_t end) {
    SieveResult result{begin, end, {}};
    if (begin >= end || end <= 2U) {
        return result;
    }
    if (end > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::length_error("reference sieve exceeds addressable memory");
    }
    std::vector<std::uint8_t> composite(static_cast<std::size_t>(end), 0U);
    composite[0] = 1U;
    composite[1] = 1U;
    for (std::uint64_t candidate = 2U; candidate <= (end - 1U) / candidate; ++candidate) {
        if (composite[static_cast<std::size_t>(candidate)] != 0U) {
            continue;
        }
        for (auto multiple = candidate * candidate; multiple < end;) {
            composite[static_cast<std::size_t>(multiple)] = 1U;
            if (multiple > (end - 1U) - candidate) {
                break;
            }
            multiple += candidate;
        }
    }
    for (auto value = std::max<std::uint64_t>(begin, 2U); value < end; ++value) {
        if (composite[static_cast<std::size_t>(value)] == 0U) {
            result.primes.push_back(value);
        }
    }
    return result;
}

SieveResult generate_primes(
    const std::uint64_t begin, const std::uint64_t end, const SieveOptions& options) {
    if (options.segment_span < 2U) {
        throw std::invalid_argument("segment_span must be at least 2");
    }
    if (options.thread_count == 0U) {
        throw std::invalid_argument("thread_count must be nonzero");
    }
    SieveResult result{begin, end, {}};
    if (begin >= end || end <= 2U) {
        return result;
    }

    const auto length = end - begin;
    const auto segment_count_u64 = length / options.segment_span +
                                   (length % options.segment_span == 0U ? 0U : 1U);
    if (segment_count_u64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::length_error("segment count exceeds addressable memory");
    }
    const auto segment_count = static_cast<std::size_t>(segment_count_u64);
    const auto base_primes = base_primes_through(integer_square_root(end - 1U));
    std::vector<Bucket> buckets(options.bucket_sieve ? segment_count : 0U);

    if (options.bucket_sieve) {
        for (const auto prime : base_primes) {
            if (prime == 2U || prime <= options.segment_span / 2U) {
                continue;
            }
            auto value = first_odd_multiple(prime, begin, end);
            const auto step = prime * 2U;
            while (value < end) {
                const auto segment = static_cast<std::size_t>((value - begin) / options.segment_span);
                auto& bucket = buckets[segment];
                if (options.bucket_layout == BucketLayout::structure_of_arrays) {
                    bucket.values.push_back(value);
                    bucket.primes.push_back(prime);
                } else {
                    bucket.entries.push_back({value, prime});
                }
                if (value > (end - 1U) - step) {
                    break;
                }
                value += step;
            }
        }
    }

    std::vector<SegmentOutput> outputs(segment_count);
    std::atomic<std::size_t> next_segment{0U};
    std::exception_ptr worker_error;
    std::mutex error_mutex;

    const auto process_segment = [&](const std::size_t segment_index) {
        const auto low = begin + static_cast<std::uint64_t>(segment_index) * options.segment_span;
        const auto remaining = end - low;
        const auto high = low + std::min(options.segment_span, remaining);
        const auto first_odd = low | 1U;
        const auto odd_count = first_odd < high
                                   ? static_cast<std::size_t>((high - first_odd + 1U) / 2U)
                                   : 0U;
        CompositeBits composite(odd_count, options.bit_packed);

        for (const auto prime : base_primes) {
            if (prime == 2U) {
                continue;
            }
            if (options.wheel_modulo_30 && (prime == 3U || prime == 5U)) {
                continue;
            }
            if (options.bucket_sieve && prime > options.segment_span / 2U) {
                continue;
            }
            auto multiple = first_odd_multiple(prime, low, high);
            const auto step = prime * 2U;
            while (multiple < high) {
                composite.set(static_cast<std::size_t>((multiple - first_odd) / 2U));
                if (multiple > (high - 1U) - step) {
                    break;
                }
                multiple += step;
            }
        }

        if (options.bucket_sieve) {
            const auto& bucket = buckets[segment_index];
            if (options.bucket_layout == BucketLayout::structure_of_arrays) {
                for (std::size_t index = 0; index < bucket.values.size(); ++index) {
                    if (options.prefetch && index + 8U < bucket.values.size()) {
                        prefetch_read(&bucket.values[index + 8U]);
                        prefetch_read(&bucket.primes[index + 8U]);
                    }
                    composite.set(static_cast<std::size_t>((bucket.values[index] - first_odd) / 2U));
                }
            } else {
                for (std::size_t index = 0; index < bucket.entries.size(); ++index) {
                    if (options.prefetch && index + 8U < bucket.entries.size()) {
                        prefetch_read(&bucket.entries[index + 8U]);
                    }
                    composite.set(
                        static_cast<std::size_t>((bucket.entries[index].value - first_odd) / 2U));
                }
            }
        }

        auto& primes = outputs[segment_index].primes;
        if (low <= 2U && 2U < high) {
            primes.push_back(2U);
        }
        for (std::size_t index = 0; index < composite.size(); ++index) {
            const auto value = first_odd + static_cast<std::uint64_t>(index) * 2U;
            if (value < 2U || composite.test(index)) {
                continue;
            }
            if (options.wheel_modulo_30 && !is_wheel_candidate(value)) {
                continue;
            }
            primes.push_back(value);
        }
    };

    const auto worker_count = std::min(options.thread_count, segment_count);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            try {
                for (;;) {
                    const auto index = next_segment.fetch_add(1U, std::memory_order_relaxed);
                    if (index >= segment_count) {
                        break;
                    }
                    process_segment(index);
                }
            } catch (...) {
                std::scoped_lock lock(error_mutex);
                if (worker_error == nullptr) {
                    worker_error = std::current_exception();
                }
                next_segment.store(segment_count, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    if (worker_error != nullptr) {
        std::rethrow_exception(worker_error);
    }
    for (auto& output : outputs) {
        result.primes.insert(result.primes.end(), output.primes.begin(), output.primes.end());
    }
    return result;
}

std::uint64_t count_primes(
    const std::uint64_t begin, const std::uint64_t end, const SieveOptions& options) {
    return static_cast<std::uint64_t>(generate_primes(begin, end, options).primes.size());
}

bool is_prime_u64(const std::uint64_t value) noexcept {
    constexpr std::array<std::uint64_t, 12> small_primes{
        2U, 3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U, 31U, 37U};
    if (value < 2U) {
        return false;
    }
    for (const auto prime : small_primes) {
        if (value == prime) {
            return true;
        }
        if (value % prime == 0U) {
            return false;
        }
    }

    auto odd_part = value - 1U;
    const auto shifts = static_cast<unsigned>(std::countr_zero(odd_part));
    odd_part >>= shifts;
    constexpr std::array<std::uint64_t, 7> witnesses{
        2U, 325U, 9'375U, 28'178U, 450'775U, 9'780'504U, 1'795'265'022U};
    for (const auto witness : witnesses) {
        if (witness % value == 0U) {
            continue;
        }
        auto residue = modular_power(witness, odd_part, value);
        if (residue == 1U || residue == value - 1U) {
            continue;
        }
        bool probably_prime = false;
        for (unsigned round = 1U; round < shifts; ++round) {
            residue = math::multiply_mod(residue, residue, value);
            if (residue == value - 1U) {
                probably_prime = true;
                break;
            }
        }
        if (!probably_prime) {
            return false;
        }
    }
    return true;
}

std::string_view to_string(const BucketLayout layout) noexcept {
    switch (layout) {
        case BucketLayout::structure_of_arrays:
            return "STRUCTURE_OF_ARRAYS";
        case BucketLayout::array_of_structures:
            return "ARRAY_OF_STRUCTURES";
    }
    return "UNKNOWN";
}

}  // namespace primeforge::sieve
