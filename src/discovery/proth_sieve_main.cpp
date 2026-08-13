// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/discovery/proth_sieve.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::uint32_t parse_u32(
    const std::string_view text, const std::string_view option) {
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || (text.size() > 1U && text.front() == '0') ||
        parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument(std::string{option} + " requires a canonical uint32");
    }
    return value;
}

struct Arguments {
    primeforge::discovery::ProthSieveConfig config;
    std::filesystem::path output;
};

[[nodiscard]] Arguments parse_arguments(const int argc, char** argv) {
    if (argc != 11) {
        throw std::invalid_argument(
            "usage: primeforge-discovery-sieve --k-start K --k-stop K --n N "
            "--sieve-bound P --output FILE");
    }
    Arguments result;
    bool have_start = false;
    bool have_stop = false;
    bool have_exponent = false;
    bool have_bound = false;
    bool have_output = false;
    for (int index = 1; index < argc; index += 2) {
        const std::string_view option{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (option == "--k-start") {
            result.config.k_start = parse_u32(value, option);
            have_start = true;
        } else if (option == "--k-stop") {
            result.config.k_stop = parse_u32(value, option);
            have_stop = true;
        } else if (option == "--n") {
            result.config.exponent = parse_u32(value, option);
            have_exponent = true;
        } else if (option == "--sieve-bound") {
            result.config.maximum_prime = parse_u32(value, option);
            have_bound = true;
        } else if (option == "--output") {
            if (value.empty()) throw std::invalid_argument("--output requires a file");
            result.output = value;
            have_output = true;
        } else {
            throw std::invalid_argument("unknown option: " + std::string{option});
        }
    }
    if (!have_start || !have_stop || !have_exponent || !have_bound || !have_output) {
        throw std::invalid_argument("every discovery-sieve option is required exactly once");
    }
    return result;
}

void write_atomic(const std::filesystem::path& output, const std::string_view content) {
    if (output.filename().empty()) throw std::invalid_argument("output path must name a file");
    const auto parent = output.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    auto temporary = output;
    temporary += ".tmp";
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        if (!stream) throw std::runtime_error("cannot create temporary survivor file");
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.flush();
        if (!stream) throw std::runtime_error("cannot commit temporary survivor file");
    }
    std::error_code error;
    std::filesystem::remove(output, error);
    error.clear();
    std::filesystem::rename(temporary, output, error);
    if (error) throw std::system_error(error, "commit survivor file");
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        const auto result = primeforge::discovery::sieve_proth_candidates(arguments.config);
        std::string content;
        content.reserve(result.survivors.size() * 16U);
        for (const auto k : result.survivors) {
            content += std::to_string(k);
            content += ' ';
            content += std::to_string(arguments.config.exponent);
            content += '\n';
        }
        write_atomic(arguments.output, content);

        const primeforge::PlatformSha256Provider sha256;
        const auto digest = sha256.digest(std::as_bytes(std::span{content.data(), content.size()}));
        std::cout << "discovery.sieve.schema=primeforge.discovery.sieve.v1\n"
                  << "discovery.sieve.k_start=" << arguments.config.k_start << '\n'
                  << "discovery.sieve.k_stop=" << arguments.config.k_stop << '\n'
                  << "discovery.sieve.n=" << arguments.config.exponent << '\n'
                  << "discovery.sieve.maximum_prime=" << arguments.config.maximum_prime << '\n'
                  << "discovery.sieve.candidates=" << result.candidate_count << '\n'
                  << "discovery.sieve.eliminated=" << result.eliminated_count << '\n'
                  << "discovery.sieve.survivors=" << result.survivors.size() << '\n'
                  << "discovery.sieve.primes_applied=" << result.primes_applied << '\n'
                  << "discovery.sieve.prime_source=primesieve-12.15-segmented\n"
                  << "discovery.sieve.inverse=direct-2^-n\n"
                  << "discovery.sieve.candidate_storage=uint64-bitset\n"
                  << "discovery.sieve.output=" << std::filesystem::absolute(arguments.output).string()
                  << '\n'
                  << "discovery.sieve.sha256=" << primeforge::sha256_to_hex(digest) << '\n'
                  << "discovery.sieve.status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-discovery-sieve: " << error.what() << '\n';
        return 1;
    }
}
