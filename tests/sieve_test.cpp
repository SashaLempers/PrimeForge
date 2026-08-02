// SPDX-License-Identifier: Apache-2.0

#include "primeforge/math/mul128.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0U;
    for (;;) {
        const auto separator = line.find('\t', start);
        if (separator == std::string::npos) {
            fields.emplace_back(line.substr(start));
            return fields;
        }
        fields.emplace_back(line.substr(start, separator - start));
        start = separator + 1U;
    }
}

[[nodiscard]] std::uint64_t parse_u64(const std::string_view text) {
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::runtime_error("invalid uint64 corpus value");
    }
    return value;
}

void test_reference_and_known_counts() {
    constexpr std::array<std::uint64_t, 10> expected{2U, 3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U, 29U};
    const auto small = primeforge::sieve::generate_primes_reference(0U, 30U);
    check(std::vector<std::uint64_t>(expected.begin(), expected.end()) == small.primes,
          "reference small-prime sequence");
    check(primeforge::sieve::count_primes(0U, 10U) == 4U, "pi(10)");
    check(primeforge::sieve::count_primes(0U, 100U) == 25U, "pi(100)");
    check(primeforge::sieve::count_primes(0U, 1'000'000U) == 78'498U, "pi(1,000,000)");
}

void test_interval_coverage_and_options() {
    const auto full_reference = primeforge::sieve::generate_primes_reference(0U, 20'000U).primes;
    constexpr std::array<std::uint64_t, 5> spans{2U, 31U, 64U, 127U, 1'024U};
    constexpr std::array<std::size_t, 3> threads{1U, 2U, 4U};
    for (const auto span : spans) {
        for (const auto thread_count : threads) {
            for (const bool wheel : {false, true}) {
                for (const bool packed : {false, true}) {
                    for (const bool bucket : {false, true}) {
                        primeforge::sieve::SieveOptions options;
                        options.segment_span = span;
                        options.thread_count = thread_count;
                        options.wheel_modulo_30 = wheel;
                        options.bit_packed = packed;
                        options.bucket_sieve = bucket;
                        options.prefetch = bucket;
                        options.bucket_layout = thread_count == 2U
                                                    ? primeforge::sieve::BucketLayout::array_of_structures
                                                    : primeforge::sieve::BucketLayout::structure_of_arrays;
                        const auto actual = primeforge::sieve::generate_primes(0U, 20'000U, options);
                        check(actual.begin == 0U && actual.end == 20'000U,
                              "half-open interval metadata preserved");
                        check(actual.primes == full_reference, "option matrix exactness");
                    }
                }
            }
        }
    }

    primeforge::sieve::SieveOptions deterministic;
    deterministic.segment_span = 113U;
    const auto reference = primeforge::sieve::generate_primes(12'345U, 98'765U, deterministic).primes;
    for (const auto thread_count : threads) {
        deterministic.thread_count = thread_count;
        check(primeforge::sieve::generate_primes(12'345U, 98'765U, deterministic).primes == reference,
              "thread-count-independent deterministic ordering");
    }

    for (std::uint64_t begin = 0U; begin < 1'000U; begin += 17U) {
        const auto end = begin + 137U;
        check(primeforge::sieve::generate_primes(begin, end).primes ==
                  primeforge::sieve::generate_primes_reference(begin, end).primes,
              "exact arbitrary half-open interval coverage");
    }
}

void test_primality_classifier_and_corpus(const std::string& corpus_path) {
    const auto primes = primeforge::sieve::generate_primes_reference(0U, 100'001U).primes;
    std::vector<bool> expected(100'001U, false);
    for (const auto prime : primes) {
        expected[static_cast<std::size_t>(prime)] = true;
    }
    for (std::uint64_t value = 0U; value <= 100'000U; ++value) {
        check(primeforge::sieve::is_prime_u64(value) == expected[static_cast<std::size_t>(value)],
              "exhaustive classifier disagreement at " + std::to_string(value));
    }

    std::ifstream input(corpus_path, std::ios::binary);
    std::string line;
    check(input.good() && static_cast<bool>(std::getline(input, line)), "open corpus for sieve test");
    std::size_t checked = 0U;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_tabs(line);
        check(fields.size() == 9U, "corpus field count");
        const auto value = parse_u64(fields[3]);
        const auto expected_prime = fields[4] == "PROVEN_PRIME";
        check(primeforge::sieve::is_prime_u64(value) == expected_prime,
              "corpus classifier disagreement: " + fields[1]);
        ++checked;
    }
    check(checked == 68U, "entire v1 corpus classified");
}

void test_mul128_differential() {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    constexpr std::array<std::uint64_t, 8> boundaries{
        0U, 1U, 2U, 0xffff'ffffU, 0x1'0000'0000U, maximum - 1U, maximum, maximum / 2U};
    for (const auto left : boundaries) {
        for (const auto right : boundaries) {
            check(primeforge::math::multiply_full(left, right) ==
                      primeforge::math::multiply_full_portable_reference(left, right),
                  "boundary full-product differential");
            for (const auto modulus : boundaries) {
                if (modulus == 0U) {
                    continue;
                }
                check(primeforge::math::multiply_mod(left, right, modulus) ==
                          primeforge::math::multiply_mod_portable_reference(left, right, modulus),
                      "boundary modular-product differential");
            }
        }
    }

    std::mt19937_64 generator{20260802U};
    for (std::size_t iteration = 0U; iteration < 200'000U; ++iteration) {
        const auto left = generator();
        const auto right = generator();
        auto modulus = generator();
        if (modulus == 0U) {
            modulus = 1U;
        }
        check(primeforge::math::multiply_full(left, right) ==
                  primeforge::math::multiply_full_portable_reference(left, right),
              "random full-product differential");
        check(primeforge::math::multiply_mod(left, right, modulus) ==
                  primeforge::math::multiply_mod_portable_reference(left, right, modulus),
              "random modular-product differential");
    }
}

void test_invalid_options() {
    auto options = primeforge::sieve::SieveOptions{};
    options.segment_span = 1U;
    bool rejected = false;
    try {
        static_cast<void>(primeforge::sieve::generate_primes(0U, 10U, options));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "segment span below two rejected");
    options.segment_span = 100U;
    options.thread_count = 0U;
    rejected = false;
    try {
        static_cast<void>(primeforge::sieve::generate_primes(0U, 10U, options));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "zero thread count rejected");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("usage: primeforge-sieve-tests <cases.tsv>");
        }
        test_reference_and_known_counts();
        test_interval_coverage_and_options();
        test_primality_classifier_and_corpus(argv[1]);
        test_mul128_differential();
        test_invalid_options();
        std::cout << "primeforge-sieve-tests: PASS; 68 corpus cases, 100001 exhaustive values, "
                     "200000 random mul128 differential cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-sieve-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
