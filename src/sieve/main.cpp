// SPDX-License-Identifier: Apache-2.0

#include "primeforge/sieve/sieve.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

template <typename Integer>
[[nodiscard]] Integer parse_integer(const std::string_view text, const std::string_view name) {
    Integer value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid value for " + std::string{name});
    }
    return value;
}

[[nodiscard]] bool parse_boolean(const std::string_view text, const std::string_view name) {
    if (text == "yes") {
        return true;
    }
    if (text == "no") {
        return false;
    }
    throw std::invalid_argument("expected yes or no for " + std::string{name});
}

int run(const int argc, char** argv) {
    std::uint64_t begin = 0U;
    std::uint64_t end = 1'000'000U;
    primeforge::sieve::SieveOptions options;
    bool generate = false;
    std::optional<std::uint64_t> primality_input;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto next = [&]() -> std::string_view {
            if (++index >= argc) {
                throw std::invalid_argument("missing value after " + std::string{argument});
            }
            return argv[index];
        };
        if (argument == "--begin") {
            begin = parse_integer<std::uint64_t>(next(), argument);
        } else if (argument == "--end") {
            end = parse_integer<std::uint64_t>(next(), argument);
        } else if (argument == "--segment-span") {
            options.segment_span = parse_integer<std::uint64_t>(next(), argument);
        } else if (argument == "--threads") {
            options.thread_count = parse_integer<std::size_t>(next(), argument);
        } else if (argument == "--wheel30") {
            options.wheel_modulo_30 = parse_boolean(next(), argument);
        } else if (argument == "--bit-packed") {
            options.bit_packed = parse_boolean(next(), argument);
        } else if (argument == "--bucket") {
            options.bucket_sieve = parse_boolean(next(), argument);
        } else if (argument == "--prefetch") {
            options.prefetch = parse_boolean(next(), argument);
        } else if (argument == "--bucket-layout") {
            const auto layout = next();
            if (layout == "soa") {
                options.bucket_layout = primeforge::sieve::BucketLayout::structure_of_arrays;
            } else if (layout == "aos") {
                options.bucket_layout = primeforge::sieve::BucketLayout::array_of_structures;
            } else {
                throw std::invalid_argument("expected soa or aos for --bucket-layout");
            }
        } else if (argument == "--generate") {
            generate = true;
        } else if (argument == "--is-prime") {
            primality_input = parse_integer<std::uint64_t>(next(), argument);
        } else {
            throw std::invalid_argument("unknown argument: " + std::string{argument});
        }
    }

    if (primality_input.has_value()) {
        std::cout << "is_prime="
                  << (primeforge::sieve::is_prime_u64(*primality_input) ? "YES" : "NO") << '\n';
        return 0;
    }

    const auto result = primeforge::sieve::generate_primes(begin, end, options);
    std::cout << "interval=[" << begin << ',' << end << ")\n"
              << "count=" << result.primes.size() << '\n'
              << "segment_span=" << options.segment_span << '\n'
              << "threads=" << options.thread_count << '\n'
              << "wheel30=" << (options.wheel_modulo_30 ? "YES" : "NO") << '\n'
              << "bit_packed=" << (options.bit_packed ? "YES" : "NO") << '\n'
              << "bucket=" << (options.bucket_sieve ? "YES" : "NO") << '\n'
              << "prefetch=" << (options.prefetch ? "YES" : "NO") << '\n'
              << "bucket_layout=" << primeforge::sieve::to_string(options.bucket_layout) << '\n';
    if (generate) {
        for (const auto prime : result.primes) {
            std::cout << prime << '\n';
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "primeforge-sieve: FAIL: " << error.what() << '\n';
        return 1;
    }
}
