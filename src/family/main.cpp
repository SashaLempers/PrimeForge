// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/family/family.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string read_definition(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open family definition: " + path.string());
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

template <typename Integer>
[[nodiscard]] Integer parse_integer(const std::string_view text, const std::string_view label) {
    Integer value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid " + std::string{label} + ": " + std::string{text});
    }
    return value;
}

void print_usage() {
    std::cout << "Usage: primeforge-family --definition FILE [--set NAME=VALUE ...] "
                 "[--modulus UINT64]\n";
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        std::filesystem::path definition_path;
        primeforge::family::Assignment assignment;
        std::uint64_t modulus = 0U;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index]};
            if (argument == "--definition" && index + 1 < argc) {
                definition_path = argv[++index];
            } else if (argument == "--set" && index + 1 < argc) {
                const std::string_view setting{argv[++index]};
                const auto separator = setting.find('=');
                if (separator == std::string_view::npos || separator == 0U ||
                    separator + 1U == setting.size()) {
                    throw std::invalid_argument("--set requires NAME=VALUE");
                }
                const std::string name{setting.substr(0U, separator)};
                if (!assignment.emplace(
                        name, parse_integer<std::int64_t>(setting.substr(separator + 1U), "value"))
                         .second) {
                    throw std::invalid_argument("duplicate --set parameter: " + name);
                }
            } else if (argument == "--modulus" && index + 1 < argc) {
                modulus = parse_integer<std::uint64_t>(argv[++index], "modulus");
                if (modulus == 0U) {
                    throw std::invalid_argument("--modulus must be nonzero");
                }
            } else if (argument == "--help") {
                print_usage();
                return 0;
            } else {
                throw std::invalid_argument("unknown or incomplete argument: " + std::string{argument});
            }
        }
        if (definition_path.empty()) {
            print_usage();
            return 2;
        }

        const auto definition = primeforge::family::parse_family(read_definition(definition_path));
        const primeforge::PortableSha256Provider sha256;
        std::cout << "family.id=" << definition.family_id << '\n'
                  << "family.canonical=" << primeforge::family::canonical_family(definition) << '\n'
                  << "family.sha256="
                  << primeforge::family::canonical_family_sha256(definition, sha256) << '\n'
                  << "family.maximum_bits_estimate="
                  << primeforge::family::estimate_maximum_bits(definition) << '\n';
        if (!assignment.empty()) {
            const auto exact = primeforge::family::evaluate_exact(definition, assignment);
            std::cout << "family.constraints_satisfied="
                      << (primeforge::family::constraints_satisfied(definition, assignment)
                              ? "true"
                              : "false")
                      << '\n'
                      << "family.exact=" << exact.to_decimal() << '\n';
            if (modulus != 0U) {
                std::cout << "family.modulo="
                          << primeforge::family::evaluate_modulo(definition, assignment, modulus)
                          << '\n';
            }
        } else if (modulus != 0U) {
            throw std::invalid_argument("--modulus requires a complete assignment");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-family: " << error.what() << '\n';
        return 1;
    }
}
