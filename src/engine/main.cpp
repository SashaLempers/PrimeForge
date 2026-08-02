// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/engine/external_adapter.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] primeforge::engine::ExternalEngineKind parse_kind(const std::string& value) {
    using Kind = primeforge::engine::ExternalEngineKind;
    const std::map<std::string, Kind> values{
        {"primesieve", Kind::primesieve}, {"flint", Kind::flint},
        {"pari-gp", Kind::pari_gp}, {"openpfgw", Kind::openpfgw},
        {"genefer22", Kind::genefer22}, {"mersenne-prpll", Kind::mersenne_prpll},
        {"mlucas", Kind::mlucas}, {"gmp-ecm", Kind::gmp_ecm}, {"fixture", Kind::fixture}};
    const auto found = values.find(value);
    if (found == values.end()) throw std::invalid_argument("unknown engine kind");
    return found->second;
}

[[nodiscard]] std::map<std::string, std::string> parse_arguments(const int argc, char** argv) {
    std::map<std::string, std::string> result;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) throw std::invalid_argument("option requires value");
        result.emplace(argv[index], argv[index + 1]);
    }
    for (const auto* required : {"--engine", "--executable", "--sha256", "--input", "--work-root", "--job-id"}) {
        if (!result.contains(required)) throw std::invalid_argument(std::string{"missing "} + required);
    }
    return result;
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        const primeforge::PortableSha256Provider sha256;
        primeforge::engine::ExternalAdapterConfig config;
        config.kind = parse_kind(arguments.at("--engine"));
        config.stable_id = "primeforge.external." + arguments.at("--engine") + ".v1";
        config.parser_version = "primeforge-external-parser-v1";
        config.executable = arguments.at("--executable");
        config.expected_executable_sha256 = arguments.at("--sha256");
        config.supported_families = {arguments.at("--engine")};
        if (arguments.contains("--timeout-ms")) {
            config.timeout = std::chrono::milliseconds{std::stoll(arguments.at("--timeout-ms"))};
        }
        if (arguments.contains("--memory-bytes")) {
            config.memory_limit_bytes = std::stoull(arguments.at("--memory-bytes"));
        }
        primeforge::engine::ExternalEngineAdapter adapter{config, sha256};
        const primeforge::EngineRequest request{
            arguments.at("--job-id"), arguments.at("--engine"), arguments.at("--input"),
            std::filesystem::path{arguments.at("--work-root")}};
        const auto result = adapter.run(request);
        std::cout << "engine_id=" << adapter.id() << '\n'
                  << "primality_status=" << primeforge::to_string(result.status.primality) << '\n'
                  << "verification_status=" << primeforge::to_string(result.status.verification) << '\n'
                  << "novelty_status=" << primeforge::to_string(result.status.novelty) << '\n'
                  << "diagnostics=" << result.diagnostics << '\n'
                  << "raw_stdout=" << result.raw_stdout_path.string() << '\n'
                  << "raw_stderr=" << result.raw_stderr_path.string() << '\n';
        return result.diagnostics == "UNKNOWN_OUTPUT_FORMAT" ||
                       result.diagnostics == "ARTIFACT_VERIFICATION_FAILED" ||
                       result.diagnostics == "PROCESS_TIMEOUT" ||
                       result.diagnostics == "PROCESS_EXIT_NONZERO"
                   ? 2
                   : 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-adapters: FAIL: " << error.what() << '\n';
        return 1;
    }
}
