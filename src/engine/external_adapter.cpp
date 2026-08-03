// SPDX-License-Identifier: Apache-2.0

#include "primeforge/engine/external_adapter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
extern char** environ;
#endif

namespace primeforge::engine {
namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot read artifact: " + path.string());
    input.seekg(0, std::ios::end);
    const auto end_position = input.tellg();
    const auto length = static_cast<std::streamoff>(end_position);
    if (length < 0 || static_cast<std::uintmax_t>(length) >
                          std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("artifact size is not representable: " + path.string());
    }
    std::string content(static_cast<std::size_t>(length), '\0');
    input.seekg(0, std::ios::beg);
    if (!content.empty()) {
        input.read(content.data(), static_cast<std::streamsize>(content.size()));
        if (input.gcount() != static_cast<std::streamsize>(content.size())) {
            throw std::runtime_error("artifact changed while reading: " + path.string());
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("artifact grew while reading: " + path.string());
    }
    return content;
}

[[nodiscard]] std::string hash_file(
    const std::filesystem::path& path, const Sha256Provider& sha256) {
    const auto content = read_file(path);
    return sha256_to_hex(sha256.digest(std::as_bytes(std::span{content.data(), content.size()})));
}

[[nodiscard]] bool contains(std::string_view text, std::string_view marker) noexcept {
    return text.find(marker) != std::string_view::npos;
}

[[nodiscard]] ArtifactVerification verify_installation(
    const ExternalAdapterConfig& config, const Sha256Provider& sha256) {
    ArtifactVerification result;
    try {
        if (!std::filesystem::is_regular_file(config.executable)) {
            result.errors.push_back("EXECUTABLE_MISSING");
        } else {
            result.executable_sha256 = hash_file(config.executable, sha256);
            if (result.executable_sha256 != config.expected_executable_sha256) {
                result.errors.push_back("EXECUTABLE_HASH_MISMATCH");
            }
        }
        for (const auto& runtime_file : config.required_runtime_files) {
            if (!std::filesystem::is_regular_file(runtime_file.path)) {
                result.errors.push_back("RUNTIME_FILE_MISSING:" + runtime_file.path.string());
            } else if (hash_file(runtime_file.path, sha256) != runtime_file.expected_sha256) {
                result.errors.push_back(
                    "RUNTIME_FILE_HASH_MISMATCH:" + runtime_file.path.string());
            }
        }
    } catch (const std::exception& error) {
        result.errors.push_back("ARTIFACT_ERROR:" + std::string{error.what()});
    }
    result.valid = result.errors.empty();
    return result;
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

class ScopedScratchDirectory {
public:
    ScopedScratchDirectory(
        const std::filesystem::path& parent, const std::size_t chunk_index) {
        const auto absolute_parent = std::filesystem::absolute(parent).lexically_normal();
        std::filesystem::create_directories(absolute_parent);
        for (std::size_t attempt = 0U; attempt < 64U; ++attempt) {
            const auto serial = next_serial_.fetch_add(1U, std::memory_order_relaxed);
            path_ = absolute_parent /
                    (".primeforge-flint-batch-" + std::to_string(chunk_index) + "-" +
                     std::to_string(serial));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
            if (error && error != std::errc::file_exists) {
                throw std::system_error(error, "create FLINT batch scratch directory");
            }
        }
        throw std::runtime_error("cannot allocate unique FLINT batch scratch directory");
    }

    ScopedScratchDirectory(const ScopedScratchDirectory&) = delete;
    ScopedScratchDirectory& operator=(const ScopedScratchDirectory&) = delete;

    ~ScopedScratchDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    inline static std::atomic_uint64_t next_serial_{};
    std::filesystem::path path_;
};

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
        output << "n=" << input
               << ";if(isprime(n),c=primecert(n);write(\"certificate.txt\",c);"
                  "if(primecertisvalid(c),print(\"PRIMEFORGE:PROVEN_PRIME\"),"
                  "print(\"PRIMEFORGE:CERTIFICATE_INVALID\")),"
                  "print(\"PRIMEFORGE:COMPOSITE\"));quit()\n";
        return {"-q", script.string()};
    }
    if (kind == ExternalEngineKind::pari_gp_certificate) {
        const auto separator = input.find('|');
        if (separator == std::string::npos) {
            throw std::invalid_argument("PARI certificate input is value|path");
        }
        const auto value = input.substr(0U, separator);
        const auto certificate = input.substr(separator + 1U);
        if (value.empty() || certificate.empty() ||
            !std::ranges::all_of(value, [](const char digit) {
                return digit >= '0' && digit <= '9';
            }) || certificate.find_first_of("\"|\\") != std::string::npos) {
            throw std::invalid_argument("unsafe PARI certificate verification input");
        }
        const auto script = working_directory / "verify-certificate.gp";
        std::ofstream output{script, std::ios::binary | std::ios::trunc};
        if (!output) throw std::runtime_error("cannot create PARI certificate verifier");
        output << "n=" << value << ";c=readvec(\"" << certificate
               << "\")[1];v=if(type(c)==\"t_INT\",c,c[1]);"
                  "if(v==n&&primecertisvalid(c),"
                  "print(\"PRIMEFORGE:CERTIFICATE_VALID\"),"
                  "print(\"PRIMEFORGE:CERTIFICATE_INVALID\"));quit()\n";
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
    const auto absolute_working_directory =
        std::filesystem::absolute(working_directory);
    const auto stdout_path = absolute_working_directory / "stdout.txt";
    const auto stderr_path = absolute_working_directory / "stderr.txt";
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
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
        absolute_working_directory.c_str(), &startup, &process);
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
    // The adapter may be invoked while another pipeline thread is active.
    // posix_spawn avoids executing allocating C++ code in a post-fork child.
    // The constant shell program receives every dynamic value positionally,
    // applies the per-process address-space limit, changes directory, and then
    // replaces itself with the requested engine without text interpolation.
    constexpr char posix_wrapper[] =
        "if [ \"$1\" != 0 ]; then ulimit -v \"$1\" || exit 125; fi; "
        "cd \"$2\" || exit 126; shift 2; exec \"$@\"";
    const auto memory_limit_kibibytes =
        memory_limit_bytes / 1'024U +
        (memory_limit_bytes % 1'024U == 0U ? 0U : 1U);
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 7U);
    storage.emplace_back("/bin/sh");
    storage.emplace_back("-c");
    storage.emplace_back(posix_wrapper);
    storage.emplace_back("primeforge-posix-spawn");
    storage.push_back(std::to_string(memory_limit_kibibytes));
    storage.push_back(absolute_working_directory.string());
    storage.push_back(executable.string());
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1U);
    for (auto& item : storage) argv.push_back(item.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t file_actions{};
    const auto actions_error = posix_spawn_file_actions_init(&file_actions);
    if (actions_error != 0) {
        throw std::system_error(
            actions_error, std::generic_category(),
            "cannot initialize posix_spawn file actions");
    }
    const auto throw_actions_error = [&](const int error, const char* const message) {
        static_cast<void>(posix_spawn_file_actions_destroy(&file_actions));
        throw std::system_error(error, std::generic_category(), message);
    };
    auto action_error = posix_spawn_file_actions_addopen(
        &file_actions, STDOUT_FILENO, stdout_path.c_str(),
        O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (action_error != 0) {
        throw_actions_error(action_error, "cannot redirect posix_spawn stdout");
    }
    action_error = posix_spawn_file_actions_addopen(
        &file_actions, STDERR_FILENO, stderr_path.c_str(),
        O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (action_error != 0) {
        throw_actions_error(action_error, "cannot redirect posix_spawn stderr");
    }
    pid_t child = -1;
    const auto spawn_error = posix_spawn(
        &child, "/bin/sh", &file_actions, nullptr, argv.data(), ::environ);
    static_cast<void>(posix_spawn_file_actions_destroy(&file_actions));
    if (spawn_error != 0) {
        throw std::system_error(
            spawn_error, std::generic_category(), "posix_spawn failed");
    }
    const auto started = std::chrono::steady_clock::now();
    int status = 0;
    bool timed_out = false;
    for (;;) {
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0) {
            if (errno == EINTR) continue;
            const auto wait_error = errno;
            static_cast<void>(kill(child, SIGKILL));
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            throw std::system_error(
                wait_error, std::generic_category(), "waitpid failed");
        }
        if (std::chrono::steady_clock::now() - started >= timeout) {
            timed_out = true;
            static_cast<void>(kill(child, SIGKILL));
            while (waitpid(child, &status, 0) < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(
                    errno, std::generic_category(),
                    "waitpid after timeout failed");
            }
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
    } else if (kind == ExternalEngineKind::pari_gp_certificate) {
        if (contains(raw_stdout, "PRIMEFORGE:CERTIFICATE_VALID")) {
            return parsed(PrimalityStatus::proven_prime,
                          "PARI_CERTIFICATE_VALID_MARKER");
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
    for (const auto& runtime_file : config_.required_runtime_files) {
        if (runtime_file.path.empty() ||
            !sha256_from_hex(runtime_file.expected_sha256).has_value()) {
            throw std::invalid_argument("invalid external runtime file requirement");
        }
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
    auto result = verify_installation(config_, *sha256_);
    try {
        if (!std::filesystem::is_regular_file(process.raw_stdout_path)) result.errors.push_back("STDOUT_MISSING");
        if (!std::filesystem::is_regular_file(process.raw_stderr_path)) result.errors.push_back("STDERR_MISSING");
    } catch (const std::exception& error) {
        result.errors.push_back("ARTIFACT_ERROR:" + std::string{error.what()});
    }
    result.valid = result.errors.empty();
    return result;
}

EngineResult ExternalEngineAdapter::run(const EngineRequest& request) {
    std::string verified_executable_sha256;
    {
        const std::scoped_lock lock{installation_mutex_};
        if (!installation_checked_) {
            installation_verification_ = verify_installation(config_, *sha256_);
            installation_checked_ = true;
        }
        if (!installation_verification_.valid) {
            const auto executable_error = std::ranges::any_of(
                installation_verification_.errors, [](const std::string& error) {
                    return error.starts_with("EXECUTABLE_") ||
                           error.starts_with("ARTIFACT_ERROR:");
                });
            return parsed(PrimalityStatus::untested,
                          executable_error ? "EXECUTABLE_PREFLIGHT_FAILED"
                                           : "RUNTIME_FILE_PREFLIGHT_FAILED");
        }
        verified_executable_sha256 = installation_verification_.executable_sha256;
    }
    const auto prepared = prepare(request);
    const auto process = run_process(prepared);
    if (!std::filesystem::is_regular_file(process.raw_stdout_path) ||
        !std::filesystem::is_regular_file(process.raw_stderr_path)) {
        auto result = parsed(PrimalityStatus::untested, "ARTIFACT_VERIFICATION_FAILED");
        result.raw_stdout_path = process.raw_stdout_path;
        result.raw_stderr_path = process.raw_stderr_path;
        return result;
    }
    auto result = parse(process);
    result.engine_executable_sha256 = std::move(verified_executable_sha256);
    if (config_.can_produce_proof &&
        result.status.primality == PrimalityStatus::proven_prime) {
        const auto certificate = prepared.working_directory / "certificate.txt";
        if (!std::filesystem::is_regular_file(certificate)) {
            return parsed(PrimalityStatus::untested, "PROOF_ARTIFACT_MISSING");
        }
        result.proof_artifact_paths.push_back(certificate);
    }
    return result;
}

std::vector<EngineResult> ExternalEngineAdapter::run_batch(
    const std::span<const EngineRequest> requests) {
    if (requests.empty()) return {};
    if (config_.kind != ExternalEngineKind::flint) {
        return EngineAdapter::run_batch(requests);
    }

    std::string verified_executable_sha256;
    {
        const std::scoped_lock lock{installation_mutex_};
        if (!installation_checked_) {
            installation_verification_ = verify_installation(config_, *sha256_);
            installation_checked_ = true;
        }
        if (!installation_verification_.valid) {
            const auto executable_error = std::ranges::any_of(
                installation_verification_.errors, [](const std::string& error) {
                    return error.starts_with("EXECUTABLE_") ||
                           error.starts_with("ARTIFACT_ERROR:");
                });
            std::vector<EngineResult> failed;
            failed.reserve(requests.size());
            for (std::size_t index = 0U; index < requests.size(); ++index) {
                failed.push_back(parsed(
                    PrimalityStatus::untested,
                    executable_error ? "EXECUTABLE_PREFLIGHT_FAILED"
                                     : "RUNTIME_FILE_PREFLIGHT_FAILED"));
            }
            return failed;
        }
        verified_executable_sha256 = installation_verification_.executable_sha256;
    }

    std::vector<std::string> arguments;
    arguments.reserve(requests.size());
    std::vector<std::filesystem::path> planned_work_directories;
    planned_work_directories.reserve(requests.size());
    std::filesystem::path batch_root;
    for (const auto& request : requests) {
        if (!supports(request)) {
            throw std::invalid_argument("external engine does not support family");
        }
        validate_job_id(request.job_id);
        const auto request_root =
            std::filesystem::absolute(request.working_directory).lexically_normal();
        if (batch_root.empty()) {
            batch_root = request_root;
        } else if (request_root != batch_root) {
            throw std::invalid_argument(
                "FLINT batch requests must share one working directory");
        }
        const auto planned_work = (request_root / request.job_id).lexically_normal();
        if (std::filesystem::exists(planned_work) ||
            std::ranges::find(planned_work_directories, planned_work) !=
                planned_work_directories.end()) {
            throw std::invalid_argument(
                "isolated work directory already exists or is duplicated");
        }
        planned_work_directories.push_back(planned_work);
        arguments.push_back(request.canonical_input);
    }
    std::vector<EngineResult> results;
    results.reserve(requests.size());
    constexpr std::size_t maximum_argument_characters = 24'000U;
    std::size_t chunk_index = 0U;
    for (std::size_t begin = 0U; begin < requests.size();) {
        std::vector<std::string> chunk_arguments;
        std::size_t argument_characters = 0U;
        std::size_t end = begin;
        while (end < requests.size()) {
            const auto estimated_characters = arguments[end].size() + 3U;
            if (end != begin &&
                argument_characters + estimated_characters > maximum_argument_characters) {
                break;
            }
            argument_characters += estimated_characters;
            chunk_arguments.push_back(arguments[end]);
            ++end;
        }
        ScopedScratchDirectory scratch{batch_root, chunk_index};
        const auto process = invoke_process(
            std::filesystem::absolute(config_.executable), chunk_arguments,
            scratch.path(), config_.timeout, config_.memory_limit_bytes);
        const auto combined_stdout = read_file(process.raw_stdout_path);
        const auto combined_stderr = read_file(process.raw_stderr_path);

        std::vector<std::string> lines;
        std::size_t offset = 0U;
        while (offset < combined_stdout.size()) {
            const auto newline = combined_stdout.find('\n', offset);
            if (newline == std::string::npos) break;
            lines.push_back(combined_stdout.substr(offset, newline - offset + 1U));
            offset = newline + 1U;
        }
        const auto chunk_size = end - begin;
        const bool output_shape_valid = offset == combined_stdout.size() &&
                                        lines.size() == chunk_size;
        for (std::size_t index = begin; index < end; ++index) {
            const auto line = output_shape_valid ? lines[index - begin] : combined_stdout;
            EngineResult result;
            if (process.timed_out) {
                result = parsed(PrimalityStatus::untested, "PROCESS_TIMEOUT");
            } else if (process.exit_code != 0) {
                result = parsed(PrimalityStatus::untested, "PROCESS_EXIT_NONZERO");
            } else if (!output_shape_valid) {
                result = parsed(PrimalityStatus::untested, "BATCH_OUTPUT_COUNT_MISMATCH");
            } else {
                result = parse_external_output(
                    config_.kind, config_.parser_version, line, combined_stderr);
            }
            result.engine_executable_sha256 = verified_executable_sha256;
            result.raw_stdout_bytes = line;
            result.raw_stderr_bytes = combined_stderr;
            results.push_back(std::move(result));
        }
        begin = end;
        ++chunk_index;
    }
    return results;
}

std::size_t ExternalEngineAdapter::recommended_parallelism() const noexcept {
    return config_.kind == ExternalEngineKind::flint ? 8U : 1U;
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
        case ExternalEngineKind::pari_gp_certificate: return "pari-gp-certificate";
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
