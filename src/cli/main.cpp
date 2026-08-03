// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/core/system_info.hpp"
#include "primeforge/engine/external_adapter.hpp"
#include "primeforge/mvp/campaign_verifier.hpp"
#include "primeforge/mvp/search_config.hpp"
#include "primeforge/mvp/search_pipeline.hpp"
#include "primeforge/prp/base2_batch.hpp"

#if defined(PRIMEFORGE_HAS_CUDA_PRP)
#include "primeforge/cuda/prp_batch.hpp"
#endif

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

volatile std::sig_atomic_t graceful_stop_requested = 0;

void handle_interrupt(const int) { graceful_stop_requested = 1; }

[[nodiscard]] std::string hash_file(const std::filesystem::path &path,
                                    const primeforge::Sha256Provider &sha256) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read engine executable: " + path.string());
    const std::string content{std::istreambuf_iterator<char>{input},
                              std::istreambuf_iterator<char>{}};
    return primeforge::sha256_to_hex(
        sha256.digest(std::as_bytes(std::span{content.data(), content.size()})));
}

void print_engine(const std::string_view name, const primeforge::mvp::EngineExecutable &engine,
                  const primeforge::Sha256Provider &sha256) {
    const auto path = std::filesystem::absolute(engine.path);
    std::cout << "engine." << name << ".path=" << path.string() << '\n';
    if (!std::filesystem::is_regular_file(path)) {
        std::cout << "engine." << name << ".availability=UNAVAILABLE\n";
        return;
    }
    const auto observed = hash_file(path, sha256);
    std::cout << "engine." << name << ".observed_sha256=" << observed << '\n'
              << "engine." << name << ".availability="
              << (observed == engine.expected_sha256 ? "VERIFIED" : "HASH_MISMATCH") << '\n';
    if (observed != engine.expected_sha256) {
        throw std::runtime_error(std::string{name} + " executable hash mismatch");
    }
}

[[nodiscard]] std::filesystem::path config_argument(const int argc, char **argv) {
    if (argc != 4 || std::string_view{argv[2]} != "--config") {
        throw std::invalid_argument("usage: primeforge <inspect|search> --config search.yaml");
    }
    return argv[3];
}

struct SearchArguments {
    std::filesystem::path config_path;
    std::optional<std::uint64_t> stop_after;
    std::string prp_backend{"auto"};
    std::size_t prp_batch_candidates{8'192U};
};

[[nodiscard]] std::uint64_t parse_positive_decimal(const std::string_view value,
                                                   const std::string_view option) {
    std::uint64_t parsed_value{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsed_value);
    if (value.empty() || (value.size() > 1U && value.front() == '0') || parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() || parsed_value == 0U) {
        throw std::invalid_argument(std::string{option} + " requires a positive canonical integer");
    }
    return parsed_value;
}

[[nodiscard]] SearchArguments search_arguments(const int argc, char **argv) {
    if (argc < 4 || argc % 2 != 0 || std::string_view{argv[2]} != "--config") {
        throw std::invalid_argument("usage: primeforge search --config search.yaml "
                                    "[--stop-after count] [--prp-backend auto|cpu|cuda] "
                                    "[--prp-batch-candidates count]");
    }
    SearchArguments result{argv[3], std::nullopt, "auto", 8'192U};
    for (int index = 4; index < argc; index += 2) {
        const std::string_view option{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (option == "--stop-after") {
            result.stop_after = parse_positive_decimal(value, option);
        } else if (option == "--prp-backend") {
            if (value != "auto" && value != "cpu" && value != "cuda") {
                throw std::invalid_argument("--prp-backend requires auto, cpu, or cuda");
            }
            result.prp_backend = value;
        } else if (option == "--prp-batch-candidates") {
            const auto count = parse_positive_decimal(value, option);
            if (count > 1'048'576U) {
                throw std::invalid_argument("--prp-batch-candidates exceeds the bounded maximum");
            }
            result.prp_batch_candidates = static_cast<std::size_t>(count);
        } else {
            throw std::invalid_argument("unknown search option: " + std::string{option});
        }
    }
    return result;
}

struct ResumeArguments {
    std::filesystem::path checkpoint_path;
    std::string prp_backend{"auto"};
    std::size_t prp_batch_candidates{8'192U};
};

[[nodiscard]] ResumeArguments resume_arguments(const int argc, char **argv) {
    if (argc < 4 || argc % 2 != 0 || std::string_view{argv[2]} != "--checkpoint") {
        throw std::invalid_argument("usage: primeforge resume --checkpoint <file> "
                                    "[--prp-backend auto|cpu|cuda] [--prp-batch-candidates count]");
    }
    ResumeArguments result{argv[3], "auto", 8'192U};
    for (int index = 4; index < argc; index += 2) {
        const std::string_view option{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (option == "--prp-backend") {
            result.prp_backend = value;
            if (result.prp_backend != "auto" && result.prp_backend != "cpu" &&
                result.prp_backend != "cuda") {
                throw std::invalid_argument("--prp-backend requires auto, cpu, or cuda");
            }
        } else if (option == "--prp-batch-candidates") {
            const auto count = parse_positive_decimal(value, option);
            if (count > 1'048'576U) {
                throw std::invalid_argument("--prp-batch-candidates exceeds the bounded maximum");
            }
            result.prp_batch_candidates = static_cast<std::size_t>(count);
        } else {
            throw std::invalid_argument("unknown resume option: " + std::string{option});
        }
    }
    return result;
}

[[nodiscard]] std::unique_ptr<primeforge::prp::Base2StrongPrpBatchBackend>
make_prp_backend(const std::string_view requested) {
    constexpr std::size_t batch_capacity = 8'192U;
    if (requested == "cpu") {
        return primeforge::prp::make_cpu_base2_strong_prp_batch_backend(batch_capacity);
    }
#if defined(PRIMEFORGE_HAS_CUDA_PRP)
    if (requested == "auto" || requested == "cuda") {
        return primeforge::cuda_backend::make_cuda_base2_strong_prp_batch_backend(batch_capacity);
    }
#else
    if (requested == "cuda") {
        throw std::runtime_error(
            "this primeforge executable was built without the CUDA PRP backend");
    }
#endif
    return primeforge::prp::make_cpu_base2_strong_prp_batch_backend(batch_capacity);
}

[[nodiscard]] std::filesystem::path named_path_argument(const int argc, char **argv,
                                                        const std::string_view option,
                                                        const std::string_view usage) {
    if (argc != 4 || std::string_view{argv[2]} != option) {
        throw std::invalid_argument(std::string{usage});
    }
    return argv[3];
}

void run_selftest() {
    const primeforge::PortableSha256Provider sha256;
    constexpr std::string_view minimal =
        "schema: primeforge.search.v1\n"
        "campaign_name: selftest\n"
        "family: proth\n"
        "expression: k*2^n+1\n"
        "parameters:\n"
        "  k:\n"
        "    start: 1\n"
        "    stop: 3\n"
        "    step: 2\n"
        "  n:\n"
        "    start: 2\n"
        "    stop: 3\n"
        "    step: 1\n"
        "constraints:\n"
        "  odd_k: true\n"
        "  k_less_than_2_pow_n: true\n"
        "work_units:\n"
        "  candidates_per_unit: 2\n"
        "sieve:\n"
        "  maximum_prime: 43\n"
        "checkpoint:\n"
        "  every_candidates: 1\n"
        "output:\n"
        "  directory: out/selftest\n"
        "engines:\n"
        "  pari_gp:\n"
        "    path: out/oracles/pari.exe\n"
        "    sha256: 0000000000000000000000000000000000000000000000000000000000000000\n"
        "  flint:\n"
        "    path: out/oracles/flint.exe\n"
        "    sha256: 0000000000000000000000000000000000000000000000000000000000000000\n";
    const auto config = primeforge::mvp::parse_search_config(minimal);
    const auto plan = primeforge::mvp::build_campaign_plan(config, sha256);
    if (!plan.coverage.valid || plan.candidate_count != 4U) {
        throw std::logic_error("MVP planning self-test failed");
    }
    std::cout << primeforge::format_system_info(primeforge::collect_system_info())
              << "mvp.plan_candidates=" << plan.candidate_count << '\n'
              << "mvp.status=PASS\n";
}

void run_inspect(const std::filesystem::path &config_path) {
    const primeforge::PortableSha256Provider sha256;
    const auto config = primeforge::mvp::load_search_config(config_path);
    const auto plan = primeforge::mvp::build_campaign_plan(config, sha256);
    std::cout << "campaign.name=" << config.campaign_name << '\n'
              << "campaign.id=" << plan.campaign_id << '\n'
              << "campaign.candidates=" << plan.candidate_count << '\n'
              << "campaign.work_units=" << plan.work_units.size() << '\n'
              << "campaign.coverage=EXACT\n";
    print_engine("pari_gp", config.pari_gp, sha256);
    print_engine("flint", config.flint, sha256);
    std::cout << "inspection.json=" << primeforge::mvp::canonical_inspection(plan) << '\n'
              << "inspect.status=PASS\n";
}

[[nodiscard]] primeforge::engine::ExternalAdapterConfig
pari_config(const primeforge::mvp::SearchConfig &config) {
    primeforge::engine::ExternalAdapterConfig pari;
    pari.kind = primeforge::engine::ExternalEngineKind::pari_gp;
    pari.stable_id = "pari-gp-2.17.4-primecert";
    pari.parser_version = "primeforge-external-parser-v1";
    pari.executable = config.pari_gp.path;
    pari.expected_executable_sha256 = config.pari_gp.expected_sha256;
    pari.supported_families = {"primeforge.proth.uint64.v1"};
    pari.timeout = std::chrono::milliseconds{30'000};
    pari.memory_limit_bytes = 512U * 1024U * 1024U;
    pari.can_produce_proof = true;
    return pari;
}

[[nodiscard]] primeforge::engine::ExternalAdapterConfig
flint_config(const primeforge::mvp::SearchConfig &config) {
    primeforge::engine::ExternalAdapterConfig flint;
    flint.kind = primeforge::engine::ExternalEngineKind::flint;
    flint.stable_id = "flint-3.6.0-independent";
    flint.parser_version = "primeforge-external-parser-v1";
    flint.executable = config.flint.path;
    flint.expected_executable_sha256 = config.flint.expected_sha256;
    const auto runtime_directory = config.flint.path.parent_path();
    flint.required_runtime_files = {
        {runtime_directory / "flint-24.dll",
         "00d4d34b091b145885368cb2737871ca98d84aadb52e7ac386fb56ac0016d08b"},
        {runtime_directory / "gmp-10.dll",
         "9909aefb265224648bc7055b305c47a7f19319410775a05a91e88081799c0677"},
        {runtime_directory / "mpfr-6.dll",
         "e1852ef40d93f08eb341aa6ba726d529879ccc067867194df1166c2026f35eb2"},
        {runtime_directory / "pthreadVC3.dll",
         "d5348d53b70d994265f776a7b6be73fd86c40ed06442f954aa6624df963cbb02"},
    };
    flint.supported_families = {"primeforge.proth.uint64.v1"};
    flint.timeout = std::chrono::milliseconds{30'000};
    flint.memory_limit_bytes = 512U * 1024U * 1024U;
    return flint;
}

void print_search_summary(const primeforge::mvp::SearchSummary &summary) {
    std::cout << "search.campaign_id=" << summary.plan.campaign_id << '\n'
              << "search.candidates=" << summary.plan.candidate_count << '\n'
              << "search.sieve_composites=" << summary.sieve_composite_count << '\n'
              << "search.base2_composites=" << summary.base2_composite_count << '\n'
              << "search.prp_backend=" << summary.prp_backend_id << '\n'
              << "search.prp_tested=" << summary.prp_tested_count << '\n'
              << "search.prp_batches=" << summary.prp_submitted_batches << '\n'
              << "search.external_classifications=" << summary.externally_classified_count << '\n'
              << "search.proven_primes=" << summary.proven_prime_count << '\n'
              << "search.composites=" << summary.composite_count << '\n'
              << "search.results=" << summary.results_path.string() << '\n'
              << "search.checkpoint=" << summary.checkpoint_path.string() << '\n'
              << "search.status=" << (summary.completed ? "PASS" : "STOPPED") << '\n';
}

void run_search(const std::filesystem::path &config_path,
                const std::optional<std::uint64_t> stop_after,
                const std::string_view prp_backend_name, const std::size_t prp_batch_candidates) {
    const primeforge::PortableSha256Provider sha256;
    const auto config = primeforge::mvp::load_search_config(config_path);
    primeforge::engine::ExternalEngineAdapter proof_engine{pari_config(config), sha256};
    primeforge::engine::ExternalEngineAdapter independent_engine{flint_config(config), sha256};
    primeforge::mvp::SearchExecutionOptions options;
    auto prp_backend = make_prp_backend(prp_backend_name);
    options.clean_stop_after_candidates = stop_after;
    options.stop_requested = [] { return graceful_stop_requested != 0; };
    options.prp_backend = prp_backend.get();
    options.prp_batch_candidates = prp_batch_candidates;
    const auto summary =
        primeforge::mvp::execute_search(config, sha256, proof_engine, independent_engine, options);
    print_search_summary(summary);
}

void run_resume(const std::filesystem::path &checkpoint_path,
                const std::string_view prp_backend_name, const std::size_t prp_batch_candidates) {
    const auto absolute_checkpoint = std::filesystem::absolute(checkpoint_path);
    const auto config_path = absolute_checkpoint.parent_path() / "search.yaml";
    const primeforge::PortableSha256Provider sha256;
    const auto config = primeforge::mvp::load_search_config(config_path);
    if (std::filesystem::absolute(config.output_directory).lexically_normal() !=
        absolute_checkpoint.parent_path().lexically_normal()) {
        throw std::runtime_error("checkpoint directory does not match recovery configuration");
    }
    primeforge::engine::ExternalEngineAdapter proof_engine{pari_config(config), sha256};
    primeforge::engine::ExternalEngineAdapter independent_engine{flint_config(config), sha256};
    primeforge::mvp::SearchExecutionOptions options;
    auto prp_backend = make_prp_backend(prp_backend_name);
    options.resume_existing = true;
    options.stop_requested = [] { return graceful_stop_requested != 0; };
    options.prp_backend = prp_backend.get();
    options.prp_batch_candidates = prp_batch_candidates;
    const auto summary =
        primeforge::mvp::execute_search(config, sha256, proof_engine, independent_engine, options);
    print_search_summary(summary);
}

void run_verify(const std::filesystem::path &results_path) {
    const auto absolute_results = std::filesystem::absolute(results_path);
    const auto config =
        primeforge::mvp::load_search_config(absolute_results.parent_path() / "search.yaml");
    const primeforge::PortableSha256Provider sha256;
    auto certificate = pari_config(config);
    certificate.kind = primeforge::engine::ExternalEngineKind::pari_gp_certificate;
    certificate.stable_id = "pari-gp-2.17.4-primecert-verifier";
    certificate.can_produce_proof = false;
    primeforge::engine::ExternalEngineAdapter certificate_verifier{certificate, sha256};
    primeforge::engine::ExternalEngineAdapter independent_engine{flint_config(config), sha256};
    const auto summary = primeforge::mvp::verify_campaign(absolute_results, sha256,
                                                          certificate_verifier, independent_engine);
    std::cout << "verify.campaign_id=" << summary.campaign_id << '\n'
              << "verify.records=" << summary.record_count << '\n'
              << "verify.proven_primes=" << summary.proven_prime_count << '\n'
              << "verify.composites=" << summary.composite_count << '\n'
              << "verify.manifest_files=" << summary.manifest_file_count << '\n'
              << "verify.status=PASS\n";
}

}  // namespace

int main(const int argc, char **argv) {
    try {
        if (argc < 2) {
            throw std::invalid_argument("usage: primeforge <selftest|inspect|search|resume|verify> "
                                        "[options]");
        }
        const std::string_view command{argv[1]};
        if (command == "search" || command == "resume") {
            std::signal(SIGINT, handle_interrupt);
        }
        if (command == "selftest" && argc == 2) {
            run_selftest();
        } else if (command == "inspect") {
            run_inspect(config_argument(argc, argv));
        } else if (command == "search") {
            const auto arguments = search_arguments(argc, argv);
            run_search(arguments.config_path, arguments.stop_after, arguments.prp_backend,
                       arguments.prp_batch_candidates);
        } else if (command == "resume") {
            const auto arguments = resume_arguments(argc, argv);
            run_resume(arguments.checkpoint_path, arguments.prp_backend,
                       arguments.prp_batch_candidates);
        } else if (command == "verify") {
            run_verify(named_path_argument(argc, argv, "--result",
                                           "usage: primeforge verify --result <results.jsonl>"));
        } else {
            throw std::invalid_argument("unknown or malformed primeforge command");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "primeforge: " << error.what() << '\n';
        return 1;
    }
}
