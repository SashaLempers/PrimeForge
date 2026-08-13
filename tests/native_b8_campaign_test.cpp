// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/discovery/native_b8_campaign.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace b8 = primeforge::discovery::native_b8;

void check(const bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

template <typename Function>
void expect_throw(Function&& function, const char* message) {
    bool threw = false;
    try { function(); } catch (const std::exception&) { threw = true; }
    check(threw, message);
}

void write_candidates(const std::filesystem::path& path, const std::vector<b8::Candidate>& candidates) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    for (const auto& candidate : candidates) { output << candidate.k << ' ' << candidate.n << '\n'; }
}

[[nodiscard]] std::string res64(const std::uint32_t k) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << k;
    return output.str();
}

[[nodiscard]] b8::BatchPhaseTimings timings() {
    b8::BatchPhaseTimings result;
    result.parameter_build_us = 10U;
    result.host_to_device_us = 11U;
    result.witness_selection_us = 12U;
    result.a_pow_k_gpu_us = 13U;
    result.main_ntt_gpu_us = 14U;
    result.gerbicz_gpu_us = 15U;
    result.final_reduce_gpu_us = 16U;
    result.device_to_host_us = 17U;
    result.worker_total_us = 108U;
    return result;
}

[[nodiscard]] b8::BatchExecution fake_execution(
    const b8::BatchRequest& request,
    const std::optional<std::size_t> prime_lane = std::nullopt) {
    b8::BatchExecution execution;
    execution.phases = timings();
    for (std::size_t lane = 0U; lane < request.candidates.size(); ++lane) {
        execution.results.push_back({
            lane, request.candidates[lane], prime_lane && *prime_lane == lane ? "PROVEN_PRIME" : "COMPOSITE",
            3U, res64(request.candidates[lane].k), "GERBICZ_PASS", {}});
    }
    return execution;
}

[[nodiscard]] std::vector<b8::Candidate> corpus(const std::size_t size) {
    std::vector<b8::Candidate> result;
    for (std::size_t index = 0U; index < size; ++index) {
        result.push_back({static_cast<std::uint32_t>(101U + 2U * index), 66'411U});
    }
    return result;
}

[[nodiscard]] b8::CampaignConfig config_for(
    const std::filesystem::path& directory,
    const std::vector<b8::Candidate>& candidates) {
    std::filesystem::create_directories(directory);
    write_candidates(directory / "remaining.txt", candidates);
    write_candidates(directory / "parent-completed.txt", {{3U, 66'411U}, {5U, 66'411U}});
    b8::CampaignConfig config;
    config.campaign_directory = directory;
    config.remaining_queue_path = directory / "remaining.txt";
    config.parent_completed_path = directory / "parent-completed.txt";
    config.campaign_id = "sha256:test-native-b8-campaign";
    config.parent_campaign_id = "sha256:test-parent";
    config.engine_commit = "test-commit";
    config.engine_binary_sha256 = std::string(64U, 'a');
    config.survivor_list_sha256 = std::string(64U, 'b');
    config.batch_size = 8U;
    config.supervisor_pid = 42U;
    return config;
}

[[nodiscard]] b8::BatchExecutor deterministic_executor(
    std::vector<std::size_t>* sizes = nullptr,
    bool* stop_after_second = nullptr) {
    return [sizes, stop_after_second](
               const b8::BatchRequest& request,
               const b8::ResourceSink& resources,
               const b8::RuntimeIdsSink& pids,
               const b8::StopRequested&) {
        if (sizes != nullptr) { sizes->push_back(request.candidates.size()); }
        pids({100U + request.batch_id, 200U + request.batch_id});
        resources({
            {"gpu_name", "fake-gpu"}, {"gpu_temperature_c", "61"}, {"gpu_power_w", "200"},
            {"gpu_throttle_reasons", "SW_POWER_CAP"}, {"watchdog_state", "CONTINUE"},
            {"WHEA_delta", "0"}, {"Xid_delta", "0"}});
        pids({});
        if (stop_after_second != nullptr && request.batch_id == 1U) { *stop_after_second = true; }
        return fake_execution(request);
    };
}

[[nodiscard]] std::vector<std::string> mathematical_rows(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string line;
    std::getline(input, line);
    std::vector<std::string> rows;
    while (std::getline(input, line)) {
        std::vector<std::string> fields;
        std::size_t begin = 0U;
        while (begin <= line.size()) {
            const auto end = line.find('\t', begin);
            fields.emplace_back(line.substr(begin, end == std::string::npos ? line.size() - begin : end - begin));
            if (end == std::string::npos) { break; }
            begin = end + 1U;
        }
        check(fields.size() == 16U, "result row width mismatch");
        rows.push_back(fields[5] + "\t" + fields[6] + "\t" + fields[10] + "\t" + fields[11] + "\t" + fields[12]);
    }
    return rows;
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_text(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

void test_parser() {
    const auto candidates = corpus(3U);
    std::string output;
    for (std::size_t lane = 0U; lane < candidates.size(); ++lane) {
        output += "PRIMEFORGE_NATIVE_BATCH_RESULT\t" + std::to_string(lane) + "\t" +
            std::to_string(candidates[lane].k) + "\t66411\t3\tCOMPOSITE\t" + res64(candidates[lane].k) + "\n";
    }
    output += "PRIMEFORGE_NATIVE_BATCH_TIMING\tparameter_build_us=10\thost_to_device_us=11\t"
        "witness_selection_us=12\ta_pow_k_gpu_us=13\tmain_ntt_gpu_us=14\tgerbicz_gpu_us=15\t"
        "final_reduce_gpu_us=16\tdevice_to_host_us=17\tworker_total_us=108\tgerbicz_status=PASS\n";
    const auto parsed = b8::parse_worker_output(output, candidates, 0);
    check(parsed.results.size() == 3U && parsed.phases.main_ntt_gpu_us == 14U,
          "production worker output parser rejected a complete batch");
    expect_throw([&] { static_cast<void>(b8::parse_worker_output(output, candidates, 7)); },
                 "nonzero worker exit was accepted");
    const auto stopped = b8::parse_worker_output("PRIMEFORGE_NATIVE_BATCH_STOPPED\n", candidates, 0);
    check(stopped.stopped && stopped.results.empty(), "clean stopped batch marker was not retained");
}

void test_tail_and_checkpoint(const std::filesystem::path& root) {
    primeforge::PortableSha256Provider sha256;
    const auto directory = root / "tail";
    const auto candidates = corpus(17U);
    const auto config = config_for(directory, candidates);
    std::vector<std::size_t> sizes;
    const auto summary = b8::run_campaign(config, deterministic_executor(&sizes), sha256);
    check(summary.state == "COMPLETE_NO_PRIME" && summary.completed_this_resume == 17U && summary.remaining == 0U,
          "17-candidate scheduler did not complete");
    check(sizes == std::vector<std::size_t>({8U, 8U, 1U}), "tail was not scheduled as B8+B8+B1");
    check(mathematical_rows(directory / "candidate-results.tsv").size() == 17U,
          "tail scheduler produced a hole or duplicate");
    const auto checkpoint = std::ifstream(directory / "campaign.checkpoint.json");
    check(static_cast<bool>(checkpoint), "durable checkpoint is missing");
    check(b8::TelemetryWriter::valid_stream_prefix(directory / "campaign-telemetry.tsv"),
          "campaign telemetry is not stream-parseable");
}

void test_stop_resume(const std::filesystem::path& root) {
    primeforge::PortableSha256Provider sha256;
    const auto candidates = corpus(25U);
    const auto interrupted_directory = root / "interrupted";
    auto interrupted_config = config_for(interrupted_directory, candidates);
    bool stopped = false;
    const auto interrupted = b8::run_campaign(
        interrupted_config, deterministic_executor(nullptr, &stopped), sha256, [&] { return stopped; });
    check(interrupted.state == "STOPPED" && interrupted.completed_this_resume == 16U && interrupted.remaining == 9U,
          "checkpoint-aligned stop did not preserve exactly two lots");
    stopped = false;
    const auto resumed = b8::run_campaign(interrupted_config, deterministic_executor(), sha256);
    check(resumed.state == "COMPLETE_NO_PRIME" && resumed.completed_this_resume == 25U,
          "resume did not finish the exact suffix");

    const auto continuous_directory = root / "continuous";
    const auto continuous_config = config_for(continuous_directory, candidates);
    const auto continuous = b8::run_campaign(continuous_config, deterministic_executor(), sha256);
    check(continuous.state == "COMPLETE_NO_PRIME" &&
              mathematical_rows(interrupted_directory / "candidate-results.tsv") ==
                  mathematical_rows(continuous_directory / "candidate-results.tsv"),
          "stop/resume mathematical results differ from continuous execution");
}

void test_result_ahead_of_checkpoint_recovery(const std::filesystem::path& root) {
    primeforge::PortableSha256Provider sha256;
    const auto directory = root / "write-ahead-recovery";
    const auto config = config_for(directory, corpus(17U));
    bool stop = false;
    const b8::BatchExecutor first = [&](const b8::BatchRequest& request, const b8::ResourceSink&,
                                        const b8::RuntimeIdsSink&, const b8::StopRequested&) {
        stop = true;
        return fake_execution(request);
    };
    const auto first_stop = b8::run_campaign(config, first, sha256, [&] { return stop; });
    check(first_stop.completed_this_resume == 8U && first_stop.state == "STOPPED",
          "write-ahead recovery setup did not stop after one batch");
    const auto lagging_checkpoint = read_text(directory / "campaign.checkpoint.json");

    stop = false;
    const b8::BatchExecutor second = [&](const b8::BatchRequest& request, const b8::ResourceSink&,
                                         const b8::RuntimeIdsSink&, const b8::StopRequested&) {
        stop = true;
        return fake_execution(request);
    };
    const auto second_stop = b8::run_campaign(config, second, sha256, [&] { return stop; });
    check(second_stop.completed_this_resume == 16U && second_stop.state == "STOPPED",
          "write-ahead recovery setup did not persist the second batch");
    write_text(directory / "campaign.checkpoint.json", lagging_checkpoint);

    std::vector<std::size_t> sizes;
    const auto recovered = b8::run_campaign(config, deterministic_executor(&sizes), sha256);
    check(recovered.state == "COMPLETE_NO_PRIME" && recovered.completed_this_resume == 17U &&
              sizes == std::vector<std::size_t>{1U} &&
              mathematical_rows(directory / "candidate-results.tsv").size() == 17U,
          "validated result file ahead of checkpoint was not recovered idempotently");
}

void test_stop_on_prime(const std::filesystem::path& root) {
    primeforge::PortableSha256Provider sha256;
    const auto directory = root / "prime";
    const auto config = config_for(directory, corpus(17U));
    std::size_t calls = 0U;
    const b8::BatchExecutor executor = [&](const b8::BatchRequest& request, const b8::ResourceSink&,
                                           const b8::RuntimeIdsSink&, const b8::StopRequested&) {
        ++calls;
        return fake_execution(request, 5U);
    };
    const auto summary = b8::run_campaign(config, executor, sha256);
    check(summary.state == "PRIME_FOUND" && summary.prime_found && summary.completed_this_resume == 8U &&
              summary.remaining == 9U && calls == 1U,
          "stop-on-prime did not retain the whole in-flight batch and stop before the next one");
    const auto rows = mathematical_rows(directory / "candidate-results.tsv");
    check(rows.size() == 8U && rows[5].find("PROVEN_PRIME") != std::string::npos,
          "non-first-lane prime or peer composite results were lost");
}

void test_telemetry_limits_and_truncation(const std::filesystem::path& root) {
    const auto directory = root / "telemetry";
    std::filesystem::create_directories(directory);
    b8::TelemetryLimits limits;
    limits.soft_limit_bytes = 3'000U;
    limits.hard_limit_bytes = 4'000U;
    limits.normal_resource_interval_seconds = 1U;
    limits.reduced_resource_interval_seconds = 10U;
    const auto path = directory / "campaign-telemetry.tsv";
    {
        b8::TelemetryWriter writer(path, "telemetry-test", limits);
        for (unsigned index = 0U; index < 20U; ++index) {
            writer.append("ERROR", "PROCESS_GLOBAL", {{"message", std::string(180U, 'x')}}, true);
        }
        check(writer.resource_interval_seconds() == 10U, "soft telemetry limit did not reduce sampling rate");
        check(!writer.append("RESOURCE_SAMPLE", "SYSTEM_GLOBAL", {{"message", std::string(2'000U, 'y')}}),
              "hard telemetry limit did not suppress optional resource samples");
        writer.append("RUN_END", "PROCESS_GLOBAL", {{"terminal_status", "STOPPED"}}, true);
        writer.flush(true);
    }
    {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output << "truncated-final-line";
    }
    check(b8::TelemetryWriter::valid_stream_prefix(path),
          "parser did not tolerate a simulated truncated final telemetry line");
    {
        b8::TelemetryWriter writer(path, "telemetry-test", limits);
        writer.append("RUN_START", "PROCESS_GLOBAL", {{"message", "recovered"}}, true);
        writer.flush(true);
    }
    check(read_text(path).find("truncated-final-line") == std::string::npos &&
              b8::TelemetryWriter::valid_stream_prefix(path),
          "telemetry writer did not discard and recover a truncated final line");
}

}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("primeforge-native-b8-tests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);
        test_parser();
        test_tail_and_checkpoint(root);
        test_stop_resume(root);
        test_result_ahead_of_checkpoint_recovery(root);
        test_stop_on_prime(root);
        test_telemetry_limits_and_truncation(root);
        std::filesystem::remove_all(root);
        std::cout << "primeforge-native-b8-tests: PASS\n"
                  << "covered=parser,B8+B8+B1,atomic-checkpoint,write-ahead-recovery,stop-resume,stop-on-prime,telemetry-soft-hard,truncated-tail\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << "primeforge-native-b8-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
