// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/proth/proth.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_failure(Function&& function, const std::string& message) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}

[[nodiscard]] std::set<std::pair<std::uint64_t, std::uint32_t>> load_known_primes(
    const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) throw std::runtime_error("cannot open known Proth corpus");
    std::set<std::pair<std::uint64_t, std::uint32_t>> result;
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::istringstream row{line};
        std::string field;
        std::getline(row, field, '\t');
        std::getline(row, field, '\t');
        std::getline(row, field, '\t');
        const auto k = std::stoull(field);
        std::getline(row, field, '\t');
        const auto n = static_cast<std::uint32_t>(std::stoul(field));
        result.emplace(k, n);
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("known corpus path required");
        const auto known_primes = load_known_primes(argv[1]);
        check(known_primes.size() == 34U, "known campaign prime cardinality");

        std::uint64_t proven = 0U;
        std::uint64_t composite = 0U;
        std::uint64_t maximum_bases_tested = 0U;
        std::uint64_t unfiltered_modular_exponentiations = 0U;
        std::uint64_t filtered_modular_exponentiations = 0U;
        std::uint64_t proven_unfiltered_modular_exponentiations = 0U;
        std::uint64_t proven_filtered_modular_exponentiations = 0U;
        for (std::uint64_t k = 1U; k <= 31U; k += 2U) {
            for (std::uint32_t n = 5U; n <= 14U; ++n) {
                const auto value = primeforge::proth::candidate_value_u64(k, n);
                const bool expected_prime = known_primes.contains({k, n});
                check(primeforge::sieve::is_prime_u64(value) == expected_prime,
                      "deterministic u64 reference disagrees with corpus");
                const auto attempt = primeforge::proth::try_prove_u64(k, n, 255U);
                maximum_bases_tested = std::max(maximum_bases_tested, attempt.bases_tested);
                unfiltered_modular_exponentiations += attempt.bases_tested;
                filtered_modular_exponentiations += attempt.modular_exponentiations;
                check(attempt.modular_exponentiations <= attempt.bases_tested,
                      "Jacobi filter cannot add modular exponentiations");
                if (expected_prime) {
                    ++proven;
                    proven_unfiltered_modular_exponentiations += attempt.bases_tested;
                    proven_filtered_modular_exponentiations +=
                        attempt.modular_exponentiations;
                    check(attempt.primality == primeforge::PrimalityStatus::proven_prime &&
                              attempt.certificate.has_value(),
                          "known prime lacks bounded Proth certificate");
                    check(attempt.modular_exponentiations >= 1U,
                          "known prime performs its proving exponentiation");
                    check(primeforge::proth::verify_u64(*attempt.certificate),
                          "generated Proth certificate does not verify");
                } else {
                    ++composite;
                    check(attempt.primality == primeforge::PrimalityStatus::untested &&
                              !attempt.certificate.has_value(),
                          "composite produced a Proth certificate");
                }
            }
        }
        check(proven == 34U && composite == 126U, "complete campaign classification");
        check(filtered_modular_exponentiations < unfiltered_modular_exponentiations,
              "Jacobi filter reduces exact modular exponentiation count");

        const auto sample = primeforge::proth::try_prove_u64(43U, 32U, 255U);
        check(sample.certificate.has_value(), "proth20 shared prime fixture proves");
        check(sample.bases_tested == 2U && sample.modular_exponentiations == 1U,
              "shared fixture skips base two before one proving exponentiation");
        auto certificate = *sample.certificate;
        const auto canonical = primeforge::proth::canonical_certificate(certificate);
        primeforge::PortableSha256Provider sha256;
        const auto digest = primeforge::proth::certificate_sha256(certificate, sha256);
        check(!canonical.empty() && canonical.back() == '}' && canonical.find('\n') == std::string::npos,
              "certificate is canonical one-line JSON");
        check(digest.size() == 64U &&
                  digest == primeforge::proth::certificate_sha256(certificate, sha256),
              "certificate hash is deterministic");
        check(digest == "7172a2acdbae79bc90671dacafa01761d5bc632f2650ad4a4674d213efccfd22",
              "certificate golden hash");
        const auto parsed = primeforge::proth::parse_canonical_certificate(canonical);
        check(parsed.has_value() && *parsed == *sample.certificate,
              "canonical certificate parser round trip");
        check(!primeforge::proth::parse_canonical_certificate(canonical + "\n").has_value(),
              "certificate parser rejects trailing bytes");
        auto padded = canonical;
        const auto k_marker = padded.find("\"k\":\"");
        padded.insert(k_marker + 5U, "0");
        check(!primeforge::proth::parse_canonical_certificate(padded).has_value(),
              "certificate parser rejects noncanonical decimal");
        auto corrupted = canonical;
        corrupted.replace(corrupted.find("\"witness\":\"3\""), 13U,
                          "\"witness\":\"4\"");
        check(!primeforge::proth::parse_canonical_certificate(corrupted).has_value(),
              "certificate parser rejects false congruence");
        ++certificate.value;
        check(!primeforge::proth::verify_u64(certificate), "mutated value rejected");
        certificate = *sample.certificate;
        ++certificate.residue;
        check(!primeforge::proth::verify_u64(certificate), "mutated residue rejected");
        certificate = *sample.certificate;
        certificate.format_version = "mutated";
        check(!primeforge::proth::verify_u64(certificate), "mutated format rejected");
        certificate = *sample.certificate;
        certificate.k = 44U;
        check(!primeforge::proth::verify_u64(certificate), "mutated k rejected");
        certificate = *sample.certificate;
        ++certificate.n;
        check(!primeforge::proth::verify_u64(certificate), "mutated n rejected");
        certificate = *sample.certificate;
        certificate.witness = 1U;
        check(!primeforge::proth::verify_u64(certificate), "mutated witness rejected");

        const auto insufficient = primeforge::proth::try_prove_u64(43U, 32U, 2U);
        check(insufficient.primality == primeforge::PrimalityStatus::untested &&
                  !insufficient.certificate.has_value(),
              "missing witness remains untested, never composite");

        check(primeforge::proth::candidate_value_u64(1U, 1U) == 3U,
              "smallest supported Proth candidate");
        expect_failure(
            [] { static_cast<void>(primeforge::proth::candidate_value_u64(2U, 3U)); },
            "even k rejected");
        expect_failure(
            [] { static_cast<void>(primeforge::proth::candidate_value_u64(3U, 1U)); },
            "k>=2^n rejected");
        expect_failure(
            [] { static_cast<void>(primeforge::proth::candidate_value_u64(3U, 63U)); },
            "overflow rejected");
        expect_failure(
            [] { static_cast<void>(primeforge::proth::try_prove_u64(1U, 1U, 1U)); },
            "invalid witness bound rejected");

        std::cout << "proth.candidates=160\n"
                  << "proth.proven=34\n"
                  << "proth.composite=126\n"
                  << "proth.maximum_bases_tested=" << maximum_bases_tested << '\n'
                  << "proth.unfiltered_modular_exponentiations="
                  << unfiltered_modular_exponentiations << '\n'
                  << "proth.filtered_modular_exponentiations="
                  << filtered_modular_exponentiations << '\n'
                  << "proth.proven_unfiltered_modular_exponentiations="
                  << proven_unfiltered_modular_exponentiations << '\n'
                  << "proth.proven_filtered_modular_exponentiations="
                  << proven_filtered_modular_exponentiations << '\n'
                  << "proth.sample_certificate_sha256=" << digest << '\n'
                  << "PrimeForge Proth tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge Proth tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
