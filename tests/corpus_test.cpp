// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CorpusCase {
    std::string id;
    std::string category;
    std::string decimal;
    std::string expected_outcome;
    std::string provenance;
    std::string seed;
};

[[nodiscard]] std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto separator = line.find('\t', start);
        if (separator == std::string::npos) {
            fields.emplace_back(line.substr(start));
            break;
        }
        fields.emplace_back(line.substr(start, separator - start));
        start = separator + 1;
    }
    return fields;
}

[[nodiscard]] bool parse_u64(std::string_view text, std::uint64_t& value) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

[[nodiscard]] std::uint64_t add_mod(std::uint64_t a, std::uint64_t b, std::uint64_t modulus) {
    return a >= modulus - b ? a - (modulus - b) : a + b;
}

[[nodiscard]] std::uint64_t multiply_mod(std::uint64_t a, std::uint64_t b, std::uint64_t modulus) {
    std::uint64_t result = 0;
    a %= modulus;
    while (b != 0) {
        if ((b & 1U) != 0U) {
            result = add_mod(result, a, modulus);
        }
        b >>= 1U;
        if (b != 0) {
            a = add_mod(a, a, modulus);
        }
    }
    return result;
}

[[nodiscard]] std::uint64_t power_mod(std::uint64_t base, std::uint64_t exponent, std::uint64_t modulus) {
    std::uint64_t result = 1;
    base %= modulus;
    while (exponent != 0) {
        if ((exponent & 1U) != 0U) {
            result = multiply_mod(result, base, modulus);
        }
        exponent >>= 1U;
        if (exponent != 0) {
            base = multiply_mod(base, base, modulus);
        }
    }
    return result;
}

[[nodiscard]] bool reference_is_prime(std::uint64_t value) {
    constexpr std::array<std::uint64_t, 12> small_primes{2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    for (const auto prime : small_primes) {
        if (value == prime) {
            return true;
        }
        if (value < 2 || value % prime == 0) {
            return false;
        }
    }

    std::uint64_t odd_part = value - 1;
    unsigned int power_of_two = 0;
    while ((odd_part & 1U) == 0U) {
        odd_part >>= 1U;
        ++power_of_two;
    }

    constexpr std::array<std::uint64_t, 7> witnesses{2, 325, 9375, 28178, 450775, 9780504, 1795265022};
    for (const auto witness : witnesses) {
        if (witness % value == 0) {
            continue;
        }
        auto residue = power_mod(witness, odd_part, value);
        if (residue == 1 || residue == value - 1) {
            continue;
        }
        bool reached_minus_one = false;
        for (unsigned int index = 1; index < power_of_two; ++index) {
            residue = multiply_mod(residue, residue, value);
            if (residue == value - 1) {
                reached_minus_one = true;
                break;
            }
        }
        if (!reached_minus_one) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool exhaustive_is_prime(std::uint64_t value) {
    if (value < 2) {
        return false;
    }
    for (std::uint64_t divisor = 2; divisor <= value / divisor; ++divisor) {
        if (value % divisor == 0) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<CorpusCase> load_corpus(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open corpus: " + path);
    }

    std::string line;
    if (!std::getline(input, line) || line != "schema_version\tid\tcategory\tdecimal\texpected_outcome\tprovenance\tseed\tform\tnotes") {
        throw std::runtime_error("unexpected corpus header");
    }

    std::vector<CorpusCase> cases;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            throw std::runtime_error("corpus must use LF line endings");
        }
        if (line.empty()) {
            continue;
        }
        const auto fields = split_tabs(line);
        if (fields.size() != 9 || fields[0] != "1") {
            throw std::runtime_error("invalid corpus record");
        }
        cases.push_back({fields[1], fields[2], fields[3], fields[4], fields[5], fields[6]});
    }
    return cases;
}

[[nodiscard]] std::string read_single_line(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    std::string line;
    if (!input || !std::getline(input, line) || line.empty() || line.back() == '\r') {
        throw std::runtime_error("invalid certificate fixture: " + path);
    }
    std::string extra;
    if (std::getline(input, extra) && !extra.empty()) {
        throw std::runtime_error("certificate fixture has trailing content: " + path);
    }
    return line;
}

int run(const std::string& corpus_path, const std::string& oracle_results_path,
        const std::string& valid_certificate_path, const std::string& corrupt_certificate_path) {
    const auto cases = load_corpus(corpus_path);
    std::set<std::string> ids;
    std::set<std::string> categories;

    for (const auto& item : cases) {
        if (!ids.insert(item.id).second) {
            throw std::runtime_error("duplicate corpus id: " + item.id);
        }
        categories.insert(item.category);
        if (item.provenance.empty()) {
            throw std::runtime_error("missing provenance: " + item.id);
        }
        if (item.category == "SEEDED_RANDOM" && item.seed != "20260802") {
            throw std::runtime_error("unexpected deterministic seed: " + item.id);
        }

        std::uint64_t value = 0;
        if (!parse_u64(item.decimal, value)) {
            throw std::runtime_error("non-canonical uint64 decimal: " + item.id);
        }
        const auto actual = value < 2 ? "REJECTED_NON_CANDIDATE" : (reference_is_prime(value) ? "PROVEN_PRIME" : "COMPOSITE");
        if (item.expected_outcome != actual) {
            throw std::runtime_error("reference disagreement: " + item.id);
        }
    }

    constexpr std::array<std::string_view, 15> required_categories{
        "DOMAIN_BOUNDARY", "SMALL_PRIME", "EVEN_COMPOSITE", "PERFECT_POWER", "PRIME_SQUARE",
        "SEMIPRIME", "CARMICHAEL", "FERMAT_BASE2_PSEUDOPRIME", "STRONG_BASE2_PSEUDOPRIME",
        "MULTIBASE_STRONG_PSEUDOPRIME", "WORD_BOUNDARY", "PROTH", "RIESEL_FORM", "MERSENNE",
        "GENERALIZED_FERMAT"};
    for (const auto category : required_categories) {
        if (!categories.contains(std::string{category})) {
            throw std::runtime_error("missing required category: " + std::string{category});
        }
    }
    if (!categories.contains("PROTH_Q_EQUALS_NUMBER") || !categories.contains("SEEDED_RANDOM")) {
        throw std::runtime_error("missing special Proth or seeded-random category");
    }

    for (std::uint64_t value = 0; value <= 100'000; ++value) {
        if (reference_is_prime(value) != exhaustive_is_prime(value)) {
            throw std::runtime_error("exhaustive enumeration disagreement at " + std::to_string(value));
        }
    }

    std::ifstream oracle_results(oracle_results_path, std::ios::binary);
    std::string line;
    if (!oracle_results || !std::getline(oracle_results, line) ||
        line != "schema_version\tcase_id\texpected_outcome\tpari_gp_2_17_4\tflint_3_6_0\tagreement") {
        throw std::runtime_error("unexpected oracle-results header");
    }
    std::set<std::string> oracle_ids;
    while (std::getline(oracle_results, line)) {
        const auto fields = split_tabs(line);
        if (fields.size() != 6) {
            throw std::runtime_error("invalid oracle evidence record");
        }
        const auto expected_oracle = fields[2] == "REJECTED_NON_CANDIDATE" ? "NOT_PRIME" : fields[2];
        if (fields[0] != "1" || fields[5] != "AGREE" ||
            expected_oracle != fields[3] || fields[3] != fields[4]) {
            throw std::runtime_error("unexplained oracle disagreement");
        }
        oracle_ids.insert(fields[1]);
    }
    if (oracle_ids != ids) {
        throw std::runtime_error("oracle evidence does not cover the exact corpus id set");
    }

    std::uint64_t valid_certificate = 0;
    std::uint64_t corrupt_certificate = 0;
    if (!parse_u64(read_single_line(valid_certificate_path), valid_certificate) ||
        !parse_u64(read_single_line(corrupt_certificate_path), corrupt_certificate) ||
        !reference_is_prime(valid_certificate) || reference_is_prime(corrupt_certificate)) {
        throw std::runtime_error("certificate fixtures do not retain their expected semantics");
    }

    std::cout << "PrimeForge corpus regression PASS: " << cases.size()
              << " cases, 100001 exhaustive values, zero oracle disagreements\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 5) {
            std::cerr << "usage: primeforge-corpus-tests <cases.tsv> <oracle_results.tsv> <valid-cert> <corrupt-cert>\n";
            return 2;
        }
        return run(argv[1], argv[2], argv[3], argv[4]);
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge corpus regression FAIL: " << error.what() << '\n';
        return 1;
    }
}
