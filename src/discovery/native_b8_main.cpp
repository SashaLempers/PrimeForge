// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/discovery/native_b8_campaign.hpp"
#include "primeforge/work/work_unit.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

using primeforge::discovery::native_b8::TelemetryFields;

std::atomic_bool signal_stop{false};
void on_signal(int) { signal_stop.store(true); }

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { throw std::runtime_error("cannot read file: " + path.string()); }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::string hash_file(
    const std::filesystem::path& path,
    const primeforge::Sha256Provider& sha256) {
    const auto bytes = read_file(path);
    return primeforge::sha256_to_hex(
        sha256.digest(std::as_bytes(std::span{bytes.data(), bytes.size()})));
}

[[nodiscard]] std::vector<std::string> split(const std::string_view value, const char separator) {
    std::vector<std::string> fields;
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const auto end = value.find(separator, begin);
        fields.emplace_back(value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin));
        if (end == std::string_view::npos) { break; }
        begin = end + 1U;
    }
    return fields;
}

[[nodiscard]] TelemetryFields read_watchdog_status(const std::filesystem::path& path) {
    TelemetryFields values;
    if (!std::filesystem::exists(path)) { return values; }
    std::string text;
    try { text = read_file(path); } catch (const std::exception&) { return values; }
    if (!text.empty() && text.back() == '\n') { text.pop_back(); }
    if (!text.empty() && text.back() == '\r') { text.pop_back(); }
    for (const auto& field : split(text, '\t')) {
        const auto equals = field.find('=');
        if (equals == std::string::npos || equals == 0U) { return {}; }
        values.emplace(field.substr(0U, equals), field.substr(equals + 1U));
    }
    if (values["schema_version"] != "1") { return {}; }
    return values;
}

#if defined(_WIN32)

[[nodiscard]] std::wstring widen(const std::string& value) {
    if (value.empty()) { return {}; }
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) { throw std::runtime_error("invalid UTF-8 process argument"); }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), count) != count) {
        throw std::runtime_error("cannot convert process argument to UTF-16");
    }
    return result;
}

[[nodiscard]] std::wstring quote_argument(const std::wstring& argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) { return argument; }
    std::wstring quoted{L'"'};
    std::size_t slashes = 0U;
    for (const wchar_t value : argument) {
        if (value == L'\\') {
            ++slashes;
        } else if (value == L'"') {
            quoted.append(slashes * 2U + 1U, L'\\');
            quoted.push_back(L'"');
            slashes = 0U;
        } else {
            quoted.append(slashes, L'\\');
            slashes = 0U;
            quoted.push_back(value);
        }
    }
    quoted.append(slashes * 2U, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess() {
        if (process_ != nullptr) {
            if (alive()) { static_cast<void>(TerminateProcess(process_, 4U)); }
            CloseHandle(process_);
        }
    }
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept
        : process_(std::exchange(other.process_, nullptr)), pid_(std::exchange(other.pid_, 0U)) {}
    ChildProcess& operator=(ChildProcess&& other) noexcept {
        if (this != &other) {
            if (process_ != nullptr) { CloseHandle(process_); }
            process_ = std::exchange(other.process_, nullptr);
            pid_ = std::exchange(other.pid_, 0U);
        }
        return *this;
    }

    [[nodiscard]] static ChildProcess launch(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::filesystem::path& stdout_path,
        const std::filesystem::path& stderr_path) {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        const HANDLE stdout_handle = CreateFileW(
            stdout_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stdout_handle == INVALID_HANDLE_VALUE) { throw std::runtime_error("cannot create child stdout log"); }
        const HANDLE stderr_handle = CreateFileW(
            stderr_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stderr_handle == INVALID_HANDLE_VALUE) {
            CloseHandle(stdout_handle);
            throw std::runtime_error("cannot create child stderr log");
        }
        std::wstring command = quote_argument(executable.wstring());
        for (const auto& argument : arguments) {
            command.push_back(L' ');
            command += quote_argument(widen(argument));
        }
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = stdout_handle;
        startup.hStdError = stderr_handle;
        PROCESS_INFORMATION information{};
        const BOOL created = CreateProcessW(
            executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, working_directory.c_str(), &startup, &information);
        CloseHandle(stdout_handle);
        CloseHandle(stderr_handle);
        if (created == FALSE) { throw std::runtime_error("CreateProcessW failed"); }
        CloseHandle(information.hThread);
        ChildProcess child;
        child.process_ = information.hProcess;
        child.pid_ = information.dwProcessId;
        return child;
    }

    [[nodiscard]] bool alive() const {
        return process_ != nullptr && WaitForSingleObject(process_, 0U) == WAIT_TIMEOUT;
    }
    [[nodiscard]] std::uint64_t pid() const noexcept { return pid_; }
    [[nodiscard]] std::optional<int> exit_code() const {
        DWORD code = STILL_ACTIVE;
        if (process_ == nullptr || GetExitCodeProcess(process_, &code) == 0 || code == STILL_ACTIVE) {
            return std::nullopt;
        }
        return static_cast<int>(code);
    }
    [[nodiscard]] bool wait(const std::uint32_t milliseconds) const {
        return process_ != nullptr && WaitForSingleObject(process_, milliseconds) == WAIT_OBJECT_0;
    }
    void terminate() {
        if (alive()) { static_cast<void>(TerminateProcess(process_, 4U)); }
    }

private:
    HANDLE process_{};
    std::uint64_t pid_{};
};

#endif

struct Arguments {
    std::filesystem::path campaign_directory;
    std::filesystem::path queue;
    std::filesystem::path parent_completed;
    std::filesystem::path engine;
    std::filesystem::path watchdog;
    std::string campaign_id;
    std::string parent_campaign_id;
    std::string engine_commit;
    std::string engine_sha256;
    std::string survivor_sha256;
    std::uint64_t supervisor_pid{};
    std::uint32_t device{};
};

[[nodiscard]] Arguments parse_arguments(const int argc, char** argv) {
    Arguments values;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto next = [&]() -> std::string {
            if (++index >= argc) { throw std::invalid_argument("missing value for " + argument); }
            return argv[index];
        };
        if (argument == "--campaign-dir") values.campaign_directory = next();
        else if (argument == "--queue") values.queue = next();
        else if (argument == "--parent-completed") values.parent_completed = next();
        else if (argument == "--engine") values.engine = next();
        else if (argument == "--watchdog") values.watchdog = next();
        else if (argument == "--campaign-id") values.campaign_id = next();
        else if (argument == "--parent-campaign-id") values.parent_campaign_id = next();
        else if (argument == "--engine-commit") values.engine_commit = next();
        else if (argument == "--engine-sha256") values.engine_sha256 = next();
        else if (argument == "--survivor-sha256") values.survivor_sha256 = next();
        else if (argument == "--supervisor-pid") values.supervisor_pid = std::stoull(next());
        else if (argument == "--device") values.device = static_cast<std::uint32_t>(std::stoul(next()));
        else throw std::invalid_argument("unknown native B8 scheduler argument: " + argument);
    }
    if (values.campaign_directory.empty() || values.queue.empty() || values.parent_completed.empty() ||
        values.engine.empty() || values.watchdog.empty() || values.campaign_id.empty() ||
        values.parent_campaign_id.empty() || values.engine_commit.empty() || values.engine_sha256.empty() ||
        values.survivor_sha256.empty() || values.supervisor_pid == 0U) {
        throw std::invalid_argument("native B8 scheduler arguments are incomplete");
    }
    values.campaign_directory = std::filesystem::absolute(values.campaign_directory);
    values.queue = std::filesystem::absolute(values.queue);
    values.parent_completed = std::filesystem::absolute(values.parent_completed);
    values.engine = std::filesystem::absolute(values.engine);
    values.watchdog = std::filesystem::absolute(values.watchdog);
    return values;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        primeforge::PortableSha256Provider sha256;
        if (!std::filesystem::is_regular_file(arguments.engine) ||
            hash_file(arguments.engine, sha256) != arguments.engine_sha256) {
            throw std::runtime_error("native B8 engine binary hash mismatch");
        }
        if (!std::filesystem::is_regular_file(arguments.watchdog)) {
            throw std::runtime_error("campaign watchdog executable is missing");
        }
        const auto operator_stop = arguments.campaign_directory / "operator.stop";
        const auto stop_requested = [&] {
            return signal_stop.load() || std::filesystem::exists(operator_stop);
        };

        const primeforge::discovery::native_b8::BatchExecutor executor =
            [&](const primeforge::discovery::native_b8::BatchRequest& request,
                const primeforge::discovery::native_b8::ResourceSink& resource_sink,
                const primeforge::discovery::native_b8::RuntimeIdsSink& runtime_sink,
                const primeforge::discovery::native_b8::StopRequested& external_stop) {
#if defined(_WIN32)
                const auto batch_directory = arguments.campaign_directory / "work" /
                    ("batch-" + std::to_string(request.batch_id) + "-attempt-" +
                     std::to_string(request.attempt_id));
                std::filesystem::create_directories(batch_directory);
                const auto worker_stop = batch_directory / "worker.stop";
                const auto watchdog_status = batch_directory / "watchdog-current.tsv";
                const auto watchdog_stderr = batch_directory / "watchdog.stderr.log";
                std::error_code ignored;
                std::filesystem::remove(worker_stop, ignored);
                std::filesystem::remove(watchdog_status, ignored);
                ChildProcess worker = ChildProcess::launch(
                    arguments.engine,
                    {"-d", std::to_string(arguments.device), "--native-batch", request.input_path.string(),
                     "--stop-on-prime", "--stop-file", worker_stop.string()},
                    batch_directory, request.stdout_path, request.stderr_path);
                ChildProcess watchdog;
                try {
                    watchdog = ChildProcess::launch(
                        arguments.watchdog,
                        {"--pid", std::to_string(worker.pid()), "--stop-file", worker_stop.string(),
                         "--watchdog-stop-file", operator_stop.string(), "--log", "NUL",
                         "--disable-detailed-log",
                         "--compact-status-file", watchdog_status.string(),
                         "--campaign", arguments.campaign_id, "--interval-ms", "2000", "--grace-ms", "30000",
                         "--max-cpu-temp-c", "92", "--max-gpu-temp-c", "88",
                         "--require-gpu-temperature", "--require-gpu-power", "--require-ram-available",
                         "--min-ram-available-bytes", "8589934592", "--require-vram-free",
                         "--min-vram-free-mib", "2048", "--require-whea-status", "--require-nvidia-xid-status"},
                        batch_directory, "NUL", watchdog_stderr);
                } catch (...) {
                    worker.terminate();
                    throw;
                }
                runtime_sink({worker.pid(), watchdog.pid()});
                std::string last_sample;
                auto last_progress = std::chrono::steady_clock::now();
                std::uintmax_t last_stdout_size{};
                bool operator_stop_written = false;
                while (worker.alive()) {
                    if (external_stop() && !operator_stop_written) {
                        primeforge::work::write_checkpoint_atomically(operator_stop, "STOP\n");
                        operator_stop_written = true;
                    }
                    if (std::filesystem::exists(watchdog_status)) {
                        auto fields = read_watchdog_status(watchdog_status);
                        if (!fields.empty()) {
                            const auto current = fields["utc"] + "\t" + fields["decision"];
                            if (current != last_sample) {
                                last_sample = current;
                                if (auto decision = fields.extract("decision"); !decision.empty()) {
                                    fields["watchdog_state"] = std::move(decision.mapped());
                                }
                                if (auto whea = fields.extract("whea_delta"); !whea.empty()) {
                                    fields["WHEA_delta"] = std::move(whea.mapped());
                                }
                                if (auto xid = fields.extract("xid_delta"); !xid.empty()) {
                                    fields["Xid_delta"] = std::move(xid.mapped());
                                }
                                resource_sink(fields);
                            }
                        }
                    }
                    const auto stdout_size = std::filesystem::exists(request.stdout_path)
                        ? std::filesystem::file_size(request.stdout_path) : 0U;
                    if (stdout_size != last_stdout_size) {
                        last_stdout_size = stdout_size;
                        last_progress = std::chrono::steady_clock::now();
                    } else if (std::chrono::steady_clock::now() - last_progress > std::chrono::minutes{30}) {
                        primeforge::work::write_checkpoint_atomically(worker_stop, "STOP\n");
                        worker.terminate();
                        watchdog.terminate();
                        throw std::runtime_error("native B8 worker made no observable progress for 30 minutes");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{200});
                }
                if (!watchdog.wait(10'000U)) {
                    watchdog.terminate();
                    throw std::runtime_error("campaign watchdog did not observe worker exit");
                }
                const auto worker_exit = worker.exit_code();
                const auto watchdog_exit = watchdog.exit_code();
                if (!worker_exit || !watchdog_exit || *watchdog_exit != 0) {
                    throw std::runtime_error("worker or watchdog exit status is unavailable or nonzero");
                }
                auto execution = primeforge::discovery::native_b8::parse_worker_output(
                    read_file(request.stdout_path), request.candidates, *worker_exit);
                TelemetryFields final_watchdog;
                for (unsigned retry = 0U; retry < 20U && final_watchdog.empty(); ++retry) {
                    final_watchdog = read_watchdog_status(watchdog_status);
                    if (final_watchdog.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds{50}); }
                }
                if (final_watchdog.empty()) {
                    throw std::runtime_error("watchdog compact status is unavailable after worker exit");
                }
                const auto reason = final_watchdog.find("reason");
                if (execution.stopped && reason != final_watchdog.end() &&
                    reason->second != "NONE" && reason->second != "EXTERNAL_GRACEFUL_STOP") {
                    execution.error = "watchdog stopped native B8 worker: " + reason->second;
                }
                runtime_sink({});
                return execution;
#else
                static_cast<void>(request);
                static_cast<void>(resource_sink);
                static_cast<void>(runtime_sink);
                static_cast<void>(external_stop);
                throw std::runtime_error("native B8 GPU production execution is currently Windows-only");
#endif
            };

        primeforge::discovery::native_b8::CampaignConfig config;
        config.campaign_directory = arguments.campaign_directory;
        config.remaining_queue_path = arguments.queue;
        config.parent_completed_path = arguments.parent_completed;
        config.campaign_id = arguments.campaign_id;
        config.parent_campaign_id = arguments.parent_campaign_id;
        config.engine_commit = arguments.engine_commit;
        config.engine_binary_sha256 = arguments.engine_sha256;
        config.survivor_list_sha256 = arguments.survivor_sha256;
        config.batch_size = 8U;
        config.supervisor_pid = arguments.supervisor_pid;
        // The first end-to-end A/B gate exceeded 3% because of lifecycle wait noise.
        // Keep the safety watchdog at 2 s, but downsample optional resource rows to 4 s.
        config.telemetry_limits.normal_resource_interval_seconds = 4U;
        const auto summary = primeforge::discovery::native_b8::run_campaign(
            config, executor, sha256, stop_requested);
        std::cout << "native_b8.state=" << summary.state << '\n'
                  << "native_b8.completed_from_parent=" << summary.completed_from_parent << '\n'
                  << "native_b8.completed_this_resume=" << summary.completed_this_resume << '\n'
                  << "native_b8.remaining=" << summary.remaining << '\n'
                  << "native_b8.next_batch_id=" << summary.next_batch_id << '\n'
                  << "native_b8.results_sha256=" << summary.campaign_results_hash << '\n';
        return summary.state == "ERROR" ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-native-b8-scheduler: " << error.what() << '\n';
        return 1;
    }
}
