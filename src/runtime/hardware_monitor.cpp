// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/hardware_monitor.hpp"

#include "primeforge/runtime/benchmark_logger.hpp"
#include "runtime_internal.hpp"

#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <powrprof.h>
#include <winhttp.h>
#else
#include <sys/wait.h>
#endif

namespace primeforge::runtime {
namespace {

[[nodiscard]] Metric unknown_metric(const std::string& source) {
    return Metric{source, "UNKNOWN"};
}

[[nodiscard]] Metric detected_metric(const std::string& source, std::string value) {
    value = internal::trim(std::move(value));
    if (value.empty() || value == "N/A" || value == "[Not Supported]") {
        return unknown_metric(source);
    }
    return Metric{source, std::move(value)};
}

[[nodiscard]] std::string metric_json(const Metric& metric) {
    return "{\"source\":" + internal::json_escape(metric.source) +
           ",\"status\":\"" + (metric.available() ? "DETECTED" : "UNKNOWN") +
           "\",\"value\":" + internal::json_escape(metric.value) + "}";
}

[[nodiscard]] std::optional<std::string> run_command(const std::string_view command) {
#ifdef _WIN32
    std::FILE* pipe = _popen(std::string(command).c_str(), "r");
#else
    std::FILE* pipe = popen(std::string(command).c_str(), "r");
#endif
    if (pipe == nullptr) {
        return std::nullopt;
    }
    std::string output;
    std::array<char, 1024> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
#ifdef _WIN32
    const int status = _pclose(pipe);
    if (status != 0) {
#else
    const int raw_status = pclose(pipe);
    if (raw_status == -1 || !WIFEXITED(raw_status) || WEXITSTATUS(raw_status) != 0) {
#endif
        return std::nullopt;
    }
    return output;
}

[[nodiscard]] std::optional<std::string> json_scalar_field(
    const std::string_view json,
    const std::string_view name) {
    const std::string marker = "\"" + std::string(name) + "\":";
    const auto marker_position = json.find(marker);
    if (marker_position == std::string_view::npos ||
        json.find(marker, marker_position + marker.size()) != std::string_view::npos) {
        return std::nullopt;
    }
    auto begin = marker_position + marker.size();
    while (begin < json.size() && (json[begin] == ' ' || json[begin] == '\t')) {
        ++begin;
    }
    if (begin == json.size() || json.substr(begin, 4U) == "null") {
        return std::nullopt;
    }
    auto end = begin;
    while (end < json.size() && json[end] != ',' && json[end] != '}' &&
           json[end] != ' ' && json[end] != '\t' && json[end] != '\r' && json[end] != '\n') {
        ++end;
    }
    if (end == begin) {
        return std::nullopt;
    }
    return std::string(json.substr(begin, end - begin));
}

[[nodiscard]] std::optional<std::string> json_string_field(
    const std::string_view json,
    const std::string_view name) {
    const std::string marker = "\"" + std::string(name) + "\":";
    const auto marker_position = json.find(marker);
    if (marker_position == std::string_view::npos ||
        json.find(marker, marker_position + marker.size()) != std::string_view::npos) {
        return std::nullopt;
    }
    auto begin = marker_position + marker.size();
    while (begin < json.size() && (json[begin] == ' ' || json[begin] == '\t')) {
        ++begin;
    }
    if (begin == json.size() || json[begin] != '"') {
        return std::nullopt;
    }
    ++begin;
    const auto end = json.find('"', begin);
    if (end == std::string_view::npos || json.substr(begin, end - begin).find('\\') != std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(json.substr(begin, end - begin));
}

[[nodiscard]] std::optional<unsigned> decimal_component(
    const std::string_view text,
    const std::size_t position,
    const std::size_t length) {
    if (position + length > text.size()) {
        return std::nullopt;
    }
    unsigned value = 0U;
    for (std::size_t index = position; index < position + length; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return std::nullopt;
        }
        value = value * 10U + static_cast<unsigned>(text[index] - '0');
    }
    return value;
}

[[nodiscard]] bool recent_utc_timestamp(const std::string_view timestamp) {
    if (timestamp.size() < 20U || timestamp[4] != '-' || timestamp[7] != '-' ||
        timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':' ||
        timestamp.back() != 'Z') {
        return false;
    }
    if (timestamp.size() > 20U) {
        if (timestamp[19] != '.' || timestamp.size() == 21U) {
            return false;
        }
        for (std::size_t index = 20U; index + 1U < timestamp.size(); ++index) {
            if (timestamp[index] < '0' || timestamp[index] > '9') {
                return false;
            }
        }
    }
    const auto year_value = decimal_component(timestamp, 0U, 4U);
    const auto month_value = decimal_component(timestamp, 5U, 2U);
    const auto day_value = decimal_component(timestamp, 8U, 2U);
    const auto hour_value = decimal_component(timestamp, 11U, 2U);
    const auto minute_value = decimal_component(timestamp, 14U, 2U);
    const auto second_value = decimal_component(timestamp, 17U, 2U);
    if (!year_value || !month_value || !day_value || !hour_value || !minute_value || !second_value ||
        *hour_value > 23U || *minute_value > 59U || *second_value > 60U) {
        return false;
    }
    const std::chrono::year_month_day calendar{
        std::chrono::year{static_cast<int>(*year_value)},
        std::chrono::month{*month_value},
        std::chrono::day{*day_value}};
    if (!calendar.ok()) {
        return false;
    }
    const auto observed = std::chrono::sys_days{calendar} +
        std::chrono::hours{*hour_value} + std::chrono::minutes{*minute_value} +
        std::chrono::seconds{*second_value};
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return observed <= now + std::chrono::seconds{2} && now - observed <= std::chrono::seconds{10};
}

#ifdef _WIN32
class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) noexcept : handle_(handle) {}
    ~WinHttpHandle() { if (handle_ != nullptr) { WinHttpCloseHandle(handle_); } }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    [[nodiscard]] HINTERNET get() const noexcept { return handle_; }

private:
    HINTERNET handle_{};
};

[[nodiscard]] std::optional<std::string> read_lconnect_local() {
    WinHttpHandle session{WinHttpOpen(
        L"PrimeForge hardware monitor/1",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0U)};
    if (session.get() == nullptr) {
        return std::nullopt;
    }
    static_cast<void>(WinHttpSetTimeouts(session.get(), 1'000, 1'000, 1'000, 1'000));
    WinHttpHandle connection{WinHttpConnect(session.get(), L"127.0.0.1", 11'021U, 0U)};
    if (connection.get() == nullptr) {
        return std::nullopt;
    }
    WinHttpHandle request{WinHttpOpenRequest(
        connection.get(),
        L"POST",
        L"/?action=SystemResource",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0U)};
    if (request.get() == nullptr ||
        WinHttpSendRequest(
            request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0U,
            WINHTTP_NO_REQUEST_DATA, 0U, 0U, 0U) == FALSE ||
        WinHttpReceiveResponse(request.get(), nullptr) == FALSE) {
        return std::nullopt;
    }
    DWORD status_code = 0U;
    DWORD status_size = sizeof(status_code);
    if (WinHttpQueryHeaders(
            request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
            WINHTTP_NO_HEADER_INDEX) == FALSE ||
        status_code != 200U) {
        return std::nullopt;
    }
    std::string response;
    while (true) {
        DWORD available = 0U;
        if (WinHttpQueryDataAvailable(request.get(), &available) == FALSE) {
            return std::nullopt;
        }
        if (available == 0U) {
            break;
        }
        if (response.size() + available > 64U * 1024U) {
            return std::nullopt;
        }
        std::vector<char> buffer(available);
        DWORD read = 0U;
        if (WinHttpReadData(request.get(), buffer.data(), available, &read) == FALSE || read == 0U) {
            return std::nullopt;
        }
        response.append(buffer.data(), read);
    }
    return response.empty() ? std::nullopt : std::optional<std::string>{std::move(response)};
}
#else
[[nodiscard]] std::optional<std::string> read_lconnect_local() {
    return std::nullopt;
}
#endif

void collect_lconnect(HardwareSnapshot& snapshot, const LocalTelemetryReader& reader) {
    const auto response = reader();
    if (!response || response->size() > 64U * 1024U) {
        return;
    }
    const auto timestamp = json_string_field(*response, "LastTime");
    if (!timestamp || !recent_utc_timestamp(*timestamp)) {
        return;
    }
    const auto assign = [&](Metric& destination, const std::string_view field,
                            const double minimum_exclusive, const double maximum_inclusive) {
        const auto text = json_scalar_field(*response, field);
        const auto numeric = text ? internal::parse_double(*text) : std::nullopt;
        if (numeric && std::isfinite(*numeric) && *numeric > minimum_exclusive &&
            *numeric <= maximum_inclusive) {
            destination = detected_metric("L-Connect.local.SystemResource." + std::string(field), *text);
        }
    };
    assign(snapshot.cpu_temperature_celsius, "CPUTemperature", 0.0, 125.0);
    assign(snapshot.cpu_power_watts, "CPUPower", 0.0, 1'000.0);
    assign(snapshot.cpu_frequency_mhz, "CPUClockRate", 0.0, 10'000.0);
}

void collect_cpu_and_ram(HardwareSnapshot& snapshot) {
#ifdef _WIN32
    struct ProcessorPowerInformation {
        ULONG number;
        ULONG maximum_mhz;
        ULONG current_mhz;
        ULONG mhz_limit;
        ULONG maximum_idle_state;
        ULONG current_idle_state;
    };
    SYSTEM_INFO system_info{};
    GetNativeSystemInfo(&system_info);
    std::vector<ProcessorPowerInformation> information(system_info.dwNumberOfProcessors);
    const auto length = static_cast<ULONG>(information.size() * sizeof(ProcessorPowerInformation));
    if (!information.empty() &&
        CallNtPowerInformation(ProcessorInformation, nullptr, 0U, information.data(), length) == ERROR_SUCCESS) {
        std::uint64_t total = 0U;
        for (const auto& processor : information) {
            total += processor.current_mhz;
        }
        snapshot.cpu_frequency_mhz = detected_metric(
            "CallNtPowerInformation.ProcessorInformation.mean_CurrentMhz",
            std::to_string(total / information.size()));
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) != 0) {
        snapshot.ram_available_bytes = detected_metric(
            "GlobalMemoryStatusEx.ullAvailPhys", std::to_string(memory.ullAvailPhys));
        snapshot.ram_used_bytes = detected_metric(
            "GlobalMemoryStatusEx.total-minus-available",
            std::to_string(memory.ullTotalPhys - memory.ullAvailPhys));
    }
#else
    std::ifstream frequency("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    std::uint64_t frequency_khz{};
    if (frequency >> frequency_khz) {
        snapshot.cpu_frequency_mhz = detected_metric(
            "sysfs.cpu0.scaling_cur_freq", std::to_string(frequency_khz / 1000U));
    }

    std::ifstream memory("/proc/meminfo");
    std::string key;
    std::uint64_t value_kib{};
    std::string unit;
    std::uint64_t total_kib{};
    std::uint64_t available_kib{};
    while (memory >> key >> value_kib >> unit) {
        if (key == "MemTotal:") { total_kib = value_kib; }
        if (key == "MemAvailable:") { available_kib = value_kib; }
    }
    if (total_kib != 0U && available_kib != 0U && total_kib >= available_kib) {
        snapshot.ram_available_bytes = detected_metric(
            "proc.meminfo.MemAvailable", std::to_string(available_kib * 1024U));
        snapshot.ram_used_bytes = detected_metric(
            "proc.meminfo.total-minus-available", std::to_string((total_kib - available_kib) * 1024U));
    }
#endif
}

void collect_nvidia(HardwareSnapshot& snapshot, const CommandRunner& runner) {
    constexpr std::string_view command =
        "nvidia-smi --query-gpu=name,uuid,temperature.gpu,temperature.memory,power.draw,power.limit,"
        "clocks.sm,clocks.mem,utilization.gpu,utilization.memory,memory.used,memory.free,"
        "memory.total,"
        "clocks_event_reasons.sw_power_cap,clocks_event_reasons.sw_thermal_slowdown,"
        "clocks_event_reasons.hw_thermal_slowdown,clocks_event_reasons.hw_power_brake_slowdown "
        "--format=csv,noheader,nounits";
    const auto output = runner(command);
    if (!output) {
        return;
    }
    const auto newline = output->find_first_of("\r\n");
    const auto fields = internal::split_csv(output->substr(0U, newline));
    if (fields.size() != 17U) {
        return;
    }
    snapshot.gpu_name = detected_metric("nvidia-smi.name", fields[0]);
    snapshot.gpu_uuid = detected_metric("nvidia-smi.uuid", fields[1]);
    snapshot.gpu_temperature_celsius = detected_metric("nvidia-smi.temperature.gpu", fields[2]);
    snapshot.gpu_memory_temperature_celsius = detected_metric("nvidia-smi.temperature.memory", fields[3]);
    snapshot.gpu_power_watts = detected_metric("nvidia-smi.power.draw", fields[4]);
    snapshot.gpu_power_limit_watts = detected_metric("nvidia-smi.power.limit", fields[5]);
    snapshot.gpu_sm_clock_mhz = detected_metric("nvidia-smi.clocks.sm", fields[6]);
    snapshot.gpu_memory_clock_mhz = detected_metric("nvidia-smi.clocks.mem", fields[7]);
    snapshot.gpu_utilization_percent = detected_metric("nvidia-smi.utilization.gpu", fields[8]);
    snapshot.gpu_memory_utilization_percent = detected_metric("nvidia-smi.utilization.memory", fields[9]);
    snapshot.vram_used_mib = detected_metric("nvidia-smi.memory.used", fields[10]);
    snapshot.vram_free_mib = detected_metric("nvidia-smi.memory.free", fields[11]);
    snapshot.vram_total_mib = detected_metric("nvidia-smi.memory.total", fields[12]);

    constexpr std::array<std::string_view, 4> reason_names{
        "SW_POWER_CAP", "SW_THERMAL_SLOWDOWN", "HW_THERMAL_SLOWDOWN", "HW_POWER_BRAKE_SLOWDOWN"};
    std::string active_reasons;
    bool reason_available = false;
    for (std::size_t index = 0U; index < reason_names.size(); ++index) {
        const auto& value = fields[index + 13U];
        if (value != "N/A" && value != "[Not Supported]") {
            reason_available = true;
        }
        if (value == "Active") {
            if (!active_reasons.empty()) { active_reasons += ','; }
            active_reasons += reason_names[index];
        }
    }
    snapshot.throttling_detected = !active_reasons.empty();
    snapshot.throttling_reasons = snapshot.throttling_detected
                                      ? active_reasons
                                      : (reason_available ? "NONE" : "UNKNOWN");
}

void collect_nvidia_xid(HardwareSnapshot& snapshot, const CommandRunner& runner) {
#ifdef _WIN32
    constexpr std::string_view command =
        "wevtutil.exe qe System "
        "/q:\"*[System[Provider[@Name='nvlddmkm'] and "
        "TimeCreated[timediff(@SystemTime) <= 120000]]]\" /c:20 /rd:true /f:text";
    const auto output = runner(command);
    if (output) {
        std::string lowered = *output;
        std::ranges::transform(lowered, lowered.begin(), [](const unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        const bool xid = lowered.find("xid") != std::string::npos ||
            lowered.find("graphics exception") != std::string::npos;
        snapshot.nvidia_xid_errors_recent = detected_metric(
            "wevtutil.System.nvlddmkm.Xid.last_120_seconds", xid ? "1" : "0");
    }
#else
    static_cast<void>(snapshot);
    static_cast<void>(runner);
#endif
}

void collect_whea(HardwareSnapshot& snapshot, const CommandRunner& runner) {
#ifdef _WIN32
    constexpr std::string_view command =
        "wevtutil.exe qe System "
        "/q:\"*[System[Provider[@Name='Microsoft-Windows-WHEA-Logger'] and "
        "TimeCreated[timediff(@SystemTime) <= 120000]]]\" /c:1 /rd:true /f:xml";
    const auto output = runner(command);
    if (output) {
        snapshot.whea_errors_recent = detected_metric(
            "wevtutil.System.WHEA-Logger.last_120_seconds",
            internal::trim(*output).empty() ? "0" : "1");
    }
#else
    static_cast<void>(snapshot);
    static_cast<void>(runner);
#endif
}

} // namespace

std::string HardwareSnapshot::canonical_json() const {
    return "{\"cpu_frequency_mhz\":" + metric_json(cpu_frequency_mhz) +
           ",\"cpu_power_watts\":" + metric_json(cpu_power_watts) +
           ",\"cpu_temperature_celsius\":" + metric_json(cpu_temperature_celsius) +
           ",\"gpu_memory_clock_mhz\":" + metric_json(gpu_memory_clock_mhz) +
           ",\"gpu_memory_temperature_celsius\":" + metric_json(gpu_memory_temperature_celsius) +
           ",\"gpu_memory_utilization_percent\":" + metric_json(gpu_memory_utilization_percent) +
           ",\"gpu_power_limit_watts\":" + metric_json(gpu_power_limit_watts) +
           ",\"gpu_power_watts\":" + metric_json(gpu_power_watts) +
           ",\"gpu_name\":" + metric_json(gpu_name) +
           ",\"gpu_sm_clock_mhz\":" + metric_json(gpu_sm_clock_mhz) +
           ",\"gpu_temperature_celsius\":" + metric_json(gpu_temperature_celsius) +
           ",\"gpu_utilization_percent\":" + metric_json(gpu_utilization_percent) +
           ",\"gpu_uuid\":" + metric_json(gpu_uuid) +
           ",\"monotonic_milliseconds\":" + internal::json_escape(std::to_string(monotonic_milliseconds)) +
           ",\"ram_available_bytes\":" + metric_json(ram_available_bytes) +
           ",\"ram_used_bytes\":" + metric_json(ram_used_bytes) +
           ",\"nvidia_xid_errors_recent\":" + metric_json(nvidia_xid_errors_recent) +
           ",\"throttling_detected\":" + (throttling_detected ? "true" : "false") +
           ",\"throttling_reasons\":" + internal::json_escape(throttling_reasons) +
           ",\"utc\":" + internal::json_escape(utc) +
           ",\"vram_free_mib\":" + metric_json(vram_free_mib) +
           ",\"vram_total_mib\":" + metric_json(vram_total_mib) +
           ",\"vram_used_mib\":" + metric_json(vram_used_mib) +
           ",\"whea_errors_recent\":" + metric_json(whea_errors_recent) + "}";
}

HardwareMonitor::HardwareMonitor(
    CommandRunner runner,
    LocalTelemetryReader local_telemetry_reader)
    : runner_(runner ? std::move(runner) : CommandRunner{run_command}),
      local_telemetry_reader_(local_telemetry_reader
                                  ? std::move(local_telemetry_reader)
                                  : LocalTelemetryReader{read_lconnect_local}) {}

HardwareSnapshot HardwareMonitor::sample() const {
    HardwareSnapshot snapshot;
    snapshot.utc = utc_now();
    snapshot.monotonic_milliseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    snapshot.cpu_frequency_mhz = unknown_metric("no validated CPU frequency provider");
    snapshot.cpu_temperature_celsius = unknown_metric("no validated CPU temperature provider");
    snapshot.cpu_power_watts = unknown_metric("no validated CPU power provider");
    snapshot.ram_used_bytes = unknown_metric("no RAM provider");
    snapshot.ram_available_bytes = unknown_metric("no RAM provider");
    snapshot.gpu_name = unknown_metric("nvidia-smi.name");
    snapshot.gpu_uuid = unknown_metric("nvidia-smi.uuid");
    snapshot.gpu_temperature_celsius = unknown_metric("nvidia-smi.temperature.gpu");
    snapshot.gpu_memory_temperature_celsius = unknown_metric("nvidia-smi.temperature.memory");
    snapshot.gpu_power_watts = unknown_metric("nvidia-smi.power.draw");
    snapshot.gpu_power_limit_watts = unknown_metric("nvidia-smi.power.limit");
    snapshot.gpu_sm_clock_mhz = unknown_metric("nvidia-smi.clocks.sm");
    snapshot.gpu_memory_clock_mhz = unknown_metric("nvidia-smi.clocks.mem");
    snapshot.gpu_utilization_percent = unknown_metric("nvidia-smi.utilization.gpu");
    snapshot.gpu_memory_utilization_percent = unknown_metric("nvidia-smi.utilization.memory");
    snapshot.vram_used_mib = unknown_metric("nvidia-smi.memory.used");
    snapshot.vram_free_mib = unknown_metric("nvidia-smi.memory.free");
    snapshot.vram_total_mib = unknown_metric("nvidia-smi.memory.total");
    snapshot.whea_errors_recent = unknown_metric("Windows WHEA event log");
    snapshot.nvidia_xid_errors_recent = unknown_metric("Windows NVIDIA Xid event log");
    collect_cpu_and_ram(snapshot);
    collect_lconnect(snapshot, local_telemetry_reader_);
    collect_whea(snapshot, runner_);
    collect_nvidia_xid(snapshot, runner_);
    collect_nvidia(snapshot, runner_);
    return snapshot;
}

} // namespace primeforge::runtime
