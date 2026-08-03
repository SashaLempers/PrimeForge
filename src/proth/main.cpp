// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/proth/proth.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        std::uint64_t k{};
        std::uint32_t n{};
        std::uint64_t maximum_witness{65'535U};
        bool have_k = false;
        bool have_n = false;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--k" && index + 1 < argc) {
                k = std::stoull(argv[++index]);
                have_k = true;
            } else if (argument == "--n" && index + 1 < argc) {
                const auto parsed = std::stoull(argv[++index]);
                if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::out_of_range("Proth exponent exceeds uint32_t");
                }
                n = static_cast<std::uint32_t>(parsed);
                have_n = true;
            } else if (argument == "--max-witness" && index + 1 < argc) {
                maximum_witness = std::stoull(argv[++index]);
            } else {
                throw std::invalid_argument(
                    "usage: primeforge-proth --k K --n N [--max-witness A]");
            }
        }
        if (!have_k || !have_n) {
            throw std::invalid_argument("--k and --n are required");
        }

        const auto value = primeforge::proth::candidate_value_u64(k, n);
        const auto attempt = primeforge::proth::try_prove_u64(k, n, maximum_witness);
        std::cout << "proth.k=" << k << '\n'
                  << "proth.n=" << n << '\n'
                  << "proth.value=" << value << '\n'
                  << "proth.bases_tested=" << attempt.bases_tested << '\n';
        if (attempt.certificate.has_value()) {
            primeforge::PortableSha256Provider sha256;
            std::cout << "proth.primality_status=PROVEN_PRIME\n"
                      << "proth.witness=" << attempt.certificate->witness << '\n'
                      << "proth.certificate="
                      << primeforge::proth::canonical_certificate(*attempt.certificate) << '\n'
                      << "proth.certificate_sha256="
                      << primeforge::proth::certificate_sha256(*attempt.certificate, sha256)
                      << '\n';
        } else if (!primeforge::sieve::is_prime_u64(value)) {
            std::cout << "proth.primality_status=COMPOSITE\n";
        } else {
            std::cout << "proth.primality_status=UNTESTED\n";
        }
        std::cout << "proth.verification_status=SELF_VERIFIED\n"
                  << "proth.novelty_status=NOT_CHECKED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-proth: " << error.what() << '\n';
        return 1;
    }
}
