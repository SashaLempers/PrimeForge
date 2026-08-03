// SPDX-License-Identifier: Apache-2.0

#include <flint/flint.h>
#include <flint/fmpz.h>

#include <iostream>
#include <string_view>

namespace {

int classify(const char* decimal) {
    fmpz_t value;
    fmpz_init(value);

    if (fmpz_set_str(value, decimal, 10) != 0) {
        fmpz_clear(value);
        std::cerr << "invalid decimal integer\n";
        return 2;
    }

    if (fmpz_cmp_ui(value, 2UL) < 0) {
        fmpz_clear(value);
        std::cout << "NOT_PRIME\n";
        return 0;
    }

    const bool is_prime = fmpz_is_prime(value) != 0;
    fmpz_clear(value);
    std::cout << (is_prime ? "PROVEN_PRIME" : "COMPOSITE") << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "FLINT " << flint_version << '\n';
        return 0;
    }
    if (argc < 2) {
        std::cerr << "usage: flint-primality-oracle <unsigned-decimal-integer>...\n";
        return 2;
    }
    for (int index = 1; index < argc; ++index) {
        const auto status = classify(argv[index]);
        if (status != 0) return status;
    }
    return 0;
}
