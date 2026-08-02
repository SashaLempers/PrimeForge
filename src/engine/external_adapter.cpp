// SPDX-License-Identifier: Apache-2.0

#include "primeforge/engine/external_adapter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace primeforge::engine {
namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read artifact: " + path.string());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string hash_file(
    const std::filesystem::path& path, const Sha256Provider& sha256) {
    const auto content = read_file(path);
    return sha256_to_hex(sha256.digest(std::as_bytes(std::span{content.data(), content.size()})));
}

[[nodiscard]] bool contains(std::string_view text, std::string_view marker) noexcept {
    return text.find(marker) != std::string_view::npos;
}

[[nodiscard]] EngineResult parsed(
    const PrimalityStatus primality, const std::string& diagnostics) {
    EngineResult result;
    result.status.primality = primality;
    result.status.verification = VerificationStatus::unverified;
    result.status.novelty = NoveltyStatus::not_checked;
    result.diagnostics = diagnostics;
    return result;
}

void validate_job_id(const std::string_view value) {
    if (value.empty() || value.size() > 128U || value == "." || value == ".." ||
        !std::ranges::all_of(value, [](const char character) {
            return (character >= 'a' && character <= 'z') ||
                   (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '-' ||
                   character == '_' || character == '.';
        })) {
        throw std::invalid_argument("unsafe external-engine job id");
    }
}

[[nodiscard]] std::vector<std::string> arguments_for(
    const ExternalEngineKind kind,
    const std::string& input,
    const std::filesystem::path& working_directory) {
    if (kind == ExternalEngineKind::primesieve) {
        const auto separator = input.find(':');
        if (separator == std::string::npos) throw std::invalid_argument("primesieve input is begin:end");
        return {input.substr(0U, separator), input.substr(separator + 1U), "--count", "--quiet", "--threads=1"};
    }
    if (kind == ExternalEngineKind::pari_gp) {
        const auto script = working_directory / "request.gp";
        std::ofstream output{script, std::ios::binary | std::ios::trunc};
        if (!output) throw std::runtime_error("cannot create PARI request");
        output << "n=" << input << ";if(isprime(n),print(\"PRIMEFORGE:PROVEN_PRIME\"),print(\"PRIMEFORGE:COMPOSITE\"));quit()\n";
        return {"-q", script.string()};
    }
    if (kind == ExternalEngineKind::fixture) {
        return {input};
    }
    return {input};
}

#if defined(_WIN32)
[[nodiscard]] std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::invalid_argument("invalid UTF-8 process argument");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

[[nodiscard]] std::wstring quote_windows(const std::wstring& value) {
    if (value.find_first_of(L" \t\"") == std::wstring::npos) return value;
    std::wstring result{L'\"'};
    std::size_t backslashes = 0U;
    for (const auto character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'\"');
            backslashes = 0U;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0U;
            result.push_back(character);
        }
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}
#endif

[[nodiscard]] ExternalProcessResult invoke_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::filesystem::path& working_directory,
    const std::chrono::milliseconds timeout,
    const std::uint64_t memory_limit_bytes) {
    const auto stdout_path = working_directory / "stdout.txt";
    const auto stderr_path = working_directory / "stderr.txt";
#if defined(_WIN32)
    const auto executable_wide = executable.wstring();
    std::wstring command = quote_windows(executable_wide);
    for (const auto& argument : arguments) {
        command.push_back(L' ');
        command += quote_windows(widen(argument));
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const auto stdout_handle = CreateFileW(stdout_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto stderr_handle = CreateFileW(stderr_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stdout_handle == INVALID_HANDLE_VALUE || stderr_handle == INVALID_HANDLE_VALUE) {
        if (stdout_handle != INVALID_HANDLE_VALUE) CloseHandle(stdout_handle);
        if (stderr_handle != INVALID_HANDLE_VALUE) CloseHandle(stderr_handle);
        throw std::runtime_error("cannot create raw process outputs");
    }
    SetHandleInformation(stdout_handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(stderr_handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = stdout_handle;
    startup.hStdError = stderr_handle;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const auto job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) throw std::runtime_error("cannot create process job object");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (memory_limit_bytes != 0U) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(memory_limit_bytes);
    }
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    const auto created = CreateProcessW(
        executable_wide.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, working_directory.c_str(), &startup, &process);
    CloseHandle(stdout_handle);
    CloseHandle(stderr_handle);
    if (created == FALSE) {
        CloseHandle(job);
        throw std::runtime_error("external process creation failed");
    }
    if (AssignProcessToJobObject(job, process.hProcess) == FALSE) {
        TerminateProcess(process.hProcess, 1U);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        throw std::runtime_error("cannot assign external process limits");
    }
    ResumeThread(process.hThread);
    const auto wait = WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeout.count()));
    const bool timed_out = wait == WAIT_TIMEOUT;
    if (timed_out) TerminateJobObject(job, 124U);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1U;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return {static_cast<int>(exit_code), timed_out, stdout_path, stderr_path};
#else
    const auto child = fork();
    if (child < 0) throw std::runtime_error("fork failed");
    if (child == 0) {
        static_cast<void>(chdir(working_directory.c_str()));
        const auto stdout_fd = open(stdout_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        const auto stderr_fd = open(stderr_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (stdout_fd < 0 || stderr_fd < 0) _exit(126);
        dup2(stdout_fd, STDOUT_FILENO);
        dup2(stderr_fd, STDERR_FILENO);
        close(stdout_fd);
        close(stderr_fd);
        if (memory_limit_bytes != 0U) {
            rlimit limit{memory_limit_bytes, memory_limit_bytes};
            setrlimit(RLIMIT_AS, &limit);
        }
        std::vector<std::string> storage;
        storage.push_back(executable.string());
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (auto& item : storage) argv.push_back(item.data());
        argv.push_back(nullptr);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    const auto started = std::chrono::steady_clock::now();
    int status = 0;
    bool timed_out = false;
    for (;;) {
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0) throw std::runtime_error("waitpid failed");
        if (std::chrono::steady_clock::now() - started >= timeout) {
            timed_out = true;
            kill(child, SIGKILL);
            waitpid(child, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    return {exit_code, timed_out, stdout_path, stderr_path};
#endif
}

}  // namespace

EngineResult parse_external_output(
    const ExternalEngineKind kind,
    const std::string_view parser_version,
    const std::string_view raw_stdout,
    const std::string_view raw_stderr) {
    if (parser_version != "primeforge-external-parser-v1") {
        throw std::invalid_argument("unsupported external parser version");
    }
    const std::string combined = std::string{raw_stdout} + "\n" + std::string{raw_stderr};
    if (kind == ExternalEngineKind::primesieve) {
        const auto first = raw_stdout.find_first_not_of(" \t\r\n");
        const auto last = raw_stdout.find_last_not_of(" \t\r\n");
        if (first != std::string_view::npos &&
            std::ranges::all_of(raw_stdout.substr(first, last - first + 1U),
                                [](const char value) { return value >= '0' && value <= '9'; })) {
            return parsed(PrimalityStatus::untested, "PRIMESIEVE_COUNT_PARSED");
        }
    } else if (kind == ExternalEngineKind::flint) {
        if (raw_stdout == "PROVEN_PRIME\n" || raw_stdout == "PROVEN_PRIME\r\n") {
            return parsed(PrimalityStatus::proven_prime, "FLINT_EXACT_PROVEN_MARKER");
        }
        if (raw_stdout == "COMPOSITE\n" || raw_stdout == "COMPOSITE\r\n") {
            return parsed(PrimalityStatus::composite, "FLINT_EXACT_COMPOSITE_MARKER");
        }
    } else if (kind == ExternalEngineKind::pari_gp) {
        if (contains(raw_stdout, "PRIMEFORGE:PROVEN_PRIME")) {
            return parsed(PrimalityStatus::proven_prime, "PARI_WRAPPER_PROVEN_MARKER");
        }
        if (contains(raw_stdout, "PRIMEFORGE:COMPOSITE")) {
            return parsed(PrimalityStatus::composite, "PARI_WRAPPER_COMPOSITE_MARKER");
        }
    } else if (kind == ExternalEngineKind::openpfgw) {
        if (contains(combined, "PRIMEFORGE_PFGW:COMPOSITE")) return parsed(PrimalityStatus::composite, "PFGW_V1_COMPOSITE");
        if (contains(combined, "PRIMEFORGE_PFGW:PRP")) return parsed(PrimalityStatus::probable_prime, "PFGW_V1_PRP");
    } else if (kind == ExternalEngineKind::genefer22) {
        if (contains(combined, "PRIMEFORGE_GENEFER:COMPOSITE")) return parsed(PrimalityStatus::composite, "GENEFER_V1_COMPOSITE");
        if (contains(combined, "PRIMEFORGE_GENEFER:PRP")) return parsed(PrimalityStatus::probable_prime, "GENEFER_V1_PRP");
    } else if (kind == ExternalEngineKind::mersenne_prpll) {
        if (contains(combined, "PRIMEFORGE_PRPLL:COMPOSITE")) return parsed(PrimalityStatus::composite, "PRPLL_V1_COMPOSITE");
        if (contains(combined, "PRIMEFORGE_PRPLL:PRP")) return parsed(PrimalityStatus::probable_prime, "PRPLL_V1_PRP");
    } else if (kind == ExternalEngineKind::mlucas) {
        if (contains(combined, "PRIMEFORGE_MLUCAS:COMPOSITE")) return parsed(PrimalityStatus::composite, "MLUCAS_V1_COMPOSITE");
        if (contains(combined, "PRIMEFORGE_MLUCAS:PRP")) return parsed(PrimalityStatus::probable_prime, "MLUCAS_V1_PRP");
    } else if (kind == ExternalEngineKind::gmp_ecm) {
        if (contains(combined, "PRIMEFORGE_ECM:FACTOR=")) return parsed(PrimalityStatus::composite, "ECM_V1_FACTOR_WITNESS");
        if (contains(combined, "PRIMEFORGE_ECM:NO_FACTOR")) return parsed(PrimalityStatus::untested, "ECM_V1_NO_FACTOR");
    } else if (kind == ExternalEngineKind::fixture) {
        if (contains(raw_stdout, "FIXTURE:COMPOSITE")) return parsed(PrimalityStatus::composite, "FIXTURE_EXACT_MARKER");
        if (contains(raw_stdout, "FIXTURE:PRP")) return parsed(PrimalityStatus::probable_prime, "FIXTURE_EXACT_MARKER");
    }
    return parsed(PrimalityStatus::untested, "UNKNOWN_OUTPUT_FORMAT");
}

ExternalEngineAdapter::ExternalEngineAdapter(
    ExternalAdapterConfig config, const Sha256Provider& sha256)
    : config_{std::move(config)}, sha256_{&sha256} {
    if (config_.stable_id.empty() || config_.parser_version.empty() || config_.executable.empty() ||
        !sha256_from_hex(config_.expected_executable_sha256).has_value()) {
        throw std::invalid_argument("incomplete external adapter configuration");
    }
}

std::string_view ExternalEngineAdapter::id() const noexcept { return config_.stable_id; }

EngineCapabilities ExternalEngineAdapter::capabilities() const {
    return {config_.supported_families, config_.kind == ExternalEngineKind::gmp_ecm,
            config_.can_produce_proof, config_.can_resume};
}

bool ExternalEngineAdapter::supports(const EngineRequest& request) const noexcept {
    return supports(request.family_id, 0U, "");
}

bool ExternalEngineAdapter::supports(
    const std::string_view family,
    const std::uint64_t bit_length,
    const std::string_view proof_policy) const noexcept {
    static_cast<void>(bit_length);
    static_cast<void>(proof_policy);
    return std::ranges::find(config_.supported_families, family) != config_.supported_families.end();
}

CostEstimate ExternalEngineAdapter::estimate_cost(const std::size_t batch_size) const noexcept {
    static_cast<void>(batch_size);
    return {};
}

PreparedExternalRun ExternalEngineAdapter::prepare(const EngineRequest& request) const {
    if (!supports(request)) throw std::invalid_argument("external engine does not support family");
    validate_job_id(request.job_id);
    const auto root = std::filesystem::absolute(request.working_directory);
    std::filesystem::create_directories(root);
    const auto work = root / request.job_id;
    if (std::filesystem::exists(work)) throw std::invalid_argument("isolated work directory already exists");
    std::filesystem::create_directory(work);
    return {request.job_id, request.family_id, request.canonical_input, work,
            work / "checkpoints", arguments_for(config_.kind, request.canonical_input, work)};
}

ExternalProcessResult ExternalEngineAdapter::run_process(const PreparedExternalRun& prepared) const {
    std::filesystem::create_directories(prepared.checkpoint_directory);
    return invoke_process(
        std::filesystem::absolute(config_.executable), prepared.arguments,
        prepared.working_directory, config_.timeout, config_.memory_limit_bytes);
}

EngineResult ExternalEngineAdapter::parse(const ExternalProcessResult& process) const {
    if (process.timed_out) return parsed(PrimalityStatus::untested, "PROCESS_TIMEOUT");
    if (process.exit_code != 0) return parsed(PrimalityStatus::untested, "PROCESS_EXIT_NONZERO");
    auto result = parse_external_output(
        config_.kind, config_.parser_version,
        read_file(process.raw_stdout_path), read_file(process.raw_stderr_path));
    result.raw_stdout_path = process.raw_stdout_path;
    result.raw_stderr_path = process.raw_stderr_path;
    return result;
}

ArtifactVerification ExternalEngineAdapter::verify_artifacts(
    const ExternalProcessResult& process) const {
    ArtifactVerification result;
    try {
        result.executable_sha256 = hash_file(config_.executable, *sha256_);
        if (result.executable_sha256 != config_.expected_executable_sha256) {
            result.errors.push_back("EXECUTABLE_HASH_MISMATCH");
        }
        if (!std::filesystem::is_regular_file(process.raw_stdout_path)) result.errors.push_back("STDOUT_MISSING");
        if (!std::filesystem::is_regular_file(process.raw_stderr_path)) result.errors.push_back("STDERR_MISSING");
    } catch (const std::exception& error) {
        result.errors.push_back("ARTIFACT_ERROR:" + std::string{error.what()});
    }
    result.valid = result.errors.empty();
    return result;
}

EngineResult ExternalEngineAdapter::run(const EngineRequest& request) {
    const auto prepared = prepare(request);
    const auto process = run_process(prepared);
    const auto verification = verify_artifacts(process);
    if (!verification.valid) {
        auto result = parsed(PrimalityStatus::untested, "ARTIFACT_VERIFICATION_FAILED");
        result.raw_stdout_path = process.raw_stdout_path;
        result.raw_stderr_path = process.raw_stderr_path;
        return result;
    }
    return parse(process);
}

std::string ExternalEngineAdapter::report_capabilities() const {
    std::ostringstream output;
    output << "id=" << config_.stable_id << ";kind=" << to_string(config_.kind)
           << ";parser=" << config_.parser_version << ";proof="
           << (config_.can_produce_proof ? "yes" : "no") << ";resume="
           << (config_.can_resume ? "yes" : "no") << ";cost=UNKNOWN";
    return output.str();
}

std::string to_string(const ExternalEngineKind kind) {
    switch (kind) {
        case ExternalEngineKind::primesieve: return "primesieve";
        case ExternalEngineKind::flint: return "flint";
        case ExternalEngineKind::pari_gp: return "pari-gp";
        case ExternalEngineKind::openpfgw: return "openpfgw";
        case ExternalEngineKind::genefer22: return "genefer22";
        case ExternalEngineKind::mersenne_prpll: return "mersenne-prpll";
        case ExternalEngineKind::mlucas: return "mlucas";
        case ExternalEngineKind::gmp_ecm: return "gmp-ecm";
        case ExternalEngineKind::fixture: return "fixture";
    }
    return "unknown";
}

}  // namespace primeforge::engine
