// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/discovery/native_batch_dispatch.hpp"
#include "primeforge/discovery/native_b8_campaign.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
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

[[nodiscard]] std::string hash_text(
    const std::string_view text,
    const primeforge::Sha256Provider& sha256) {
    return primeforge::sha256_to_hex(
        sha256.digest(std::as_bytes(std::span{text.data(), text.size()})));
}

[[nodiscard]] std::string last_result_batch_hash(const std::filesystem::path& path) {
    auto content = read_text(path);
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) { content.pop_back(); }
    const auto line_begin = content.find_last_of('\n');
    const auto field_begin = content.find_last_of('\t');
    check(field_begin != std::string::npos &&
              (line_begin == std::string::npos || field_begin > line_begin),
          "cannot recover the last durable batch hash");
    return content.substr(field_begin + 1U);
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

void test_batch_dispatch() {
    using primeforge::discovery::NativeBatchDispatchRequest;
    using primeforge::discovery::select_native_batch;
    const auto small = select_native_batch({"NVIDIA GeForce RTX 5080", 65'536U, 32U, {}});
    check(small.batch_size == 32U && small.plan_mode == "ENGINE_AUTOTUNE" &&
              small.plan_policy_id == "primeforge.native-plan.proth20-autotune.v1",
          "RTX 5080 65536-transform dispatch mismatch");
    check(select_native_batch({"NVIDIA GeForce RTX 5080", 131'072U, 32U, {}}).batch_size == 16U,
          "RTX 5080 131072-transform dispatch mismatch");
    check(select_native_batch({"NVIDIA GeForce RTX 5080", 262'144U, 32U, {}}).batch_size == 8U,
          "RTX 5080 262144-transform dispatch mismatch");
    check(select_native_batch({"NVIDIA GeForce RTX 5080", 131'072U, 8U, {}}).batch_size == 8U,
          "dispatch did not respect engine capacity");
    check(select_native_batch({"unknown GPU", 65'536U, 32U, {}}).batch_size == 1U &&
              select_native_batch({"NVIDIA GeForce RTX 5080", 0U, 32U, {}}).batch_size == 1U &&
              select_native_batch({"NVIDIA GeForce RTX 5080", 524'288U, 32U, {}}).batch_size == 1U,
          "unmeasured dispatch did not choose the safe B1 fallback");
    check(select_native_batch(NativeBatchDispatchRequest{
              "unknown GPU", 0U, 32U, std::optional<std::uint32_t>{16U}}).batch_size == 16U,
          "explicit dispatch request was not preserved");
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

void test_b32_tail(const std::filesystem::path& root) {
    primeforge::PortableSha256Provider sha256;
    const auto directory = root / "b32-tail";
    const auto candidates = corpus(65U);
    auto config = config_for(directory, candidates);
    config.batch_size = b8::max_batch_size;
    std::vector<std::size_t> sizes;
    const auto summary = b8::run_campaign(config, deterministic_executor(&sizes), sha256);
    check(summary.state == "COMPLETE_NO_PRIME" && summary.completed_this_resume == 65U &&
              summary.remaining == 0U,
          "65-candidate B32 scheduler did not complete");
    check(sizes == std::vector<std::size_t>({32U, 32U, 1U}),
          "tail was not scheduled as B32+B32+B1");
    check(mathematical_rows(directory / "candidate-results.tsv").size() == 65U,
          "B32 tail scheduler produced a hole or duplicate");
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

void test_b32_stop_resume(const std::filesystem::path& root) {
    primeforge::PortableSha256Provider sha256;
    const auto candidates = corpus(65U);
    const auto interrupted_directory = root / "b32-interrupted";
    auto interrupted_config = config_for(interrupted_directory, candidates);
    interrupted_config.batch_size = b8::max_batch_size;
    bool stopped = false;
    const auto interrupted = b8::run_campaign(
        interrupted_config, deterministic_executor(nullptr, &stopped), sha256, [&] { return stopped; });
    check(interrupted.state == "STOPPED" && interrupted.completed_this_resume == 64U &&
              interrupted.remaining == 1U,
          "B32 checkpoint-aligned stop did not preserve exactly two lots");
    stopped = false;
    const auto resumed = b8::run_campaign(interrupted_config, deterministic_executor(), sha256);
    check(resumed.state == "COMPLETE_NO_PRIME" && resumed.completed_this_resume == 65U,
          "B32 resume did not finish the exact suffix");

    const auto continuous_directory = root / "b32-continuous";
    auto continuous_config = config_for(continuous_directory, candidates);
    continuous_config.batch_size = b8::max_batch_size;
    static_cast<void>(b8::run_campaign(continuous_config, deterministic_executor(), sha256));
    check(mathematical_rows(interrupted_directory / "candidate-results.tsv") ==
              mathematical_rows(continuous_directory / "candidate-results.tsv"),
          "B32 stop/resume mathematical results differ from continuous execution");
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

void test_incomplete_appended_batch_recovery(const std::filesystem::path& root) {
    primeforge::PortableSha256Provider sha256;
    const auto directory = root / "incomplete-appended-batch";
    const auto candidates = corpus(17U);
    const auto config = config_for(directory, candidates);
    bool stop = false;
    const b8::BatchExecutor first = [&](const b8::BatchRequest& request, const b8::ResourceSink&,
                                        const b8::RuntimeIdsSink&, const b8::StopRequested&) {
        stop = true;
        return fake_execution(request);
    };
    const auto stopped = b8::run_campaign(config, first, sha256, [&] { return stop; });
    check(stopped.state == "STOPPED" && stopped.completed_this_resume == 8U,
          "incomplete append recovery setup did not persist one batch");

    const auto result_path = directory / "candidate-results.tsv";
    std::istringstream input(read_text(result_path));
    std::string truncated;
    std::string line;
    for (std::size_t index = 0U; index < 5U && std::getline(input, line); ++index) {
        truncated += line + "\n";
    }
    write_text(result_path, truncated);
    std::filesystem::remove(directory / "campaign.checkpoint.json");

    std::vector<std::size_t> sizes;
    const auto recovered = b8::run_campaign(config, deterministic_executor(&sizes), sha256);
    check(recovered.state == "COMPLETE_NO_PRIME" && recovered.completed_this_resume == 17U &&
              sizes == std::vector<std::size_t>({8U, 8U, 1U}) &&
              mathematical_rows(result_path).size() == 17U,
          "incomplete final append was not rolled back to the last complete batch");
}

void test_schema_v1_checkpoint_migration(const std::filesystem::path& root) {
    primeforge::PortableSha256Provider sha256;
    const auto directory = root / "schema-v1-migration";
    const auto candidates = corpus(17U);
    const auto config = config_for(directory, candidates);
    bool stop = false;
    const b8::BatchExecutor first = [&](const b8::BatchRequest& request, const b8::ResourceSink&,
                                        const b8::RuntimeIdsSink&, const b8::StopRequested&) {
        stop = true;
        return fake_execution(request);
    };
    const auto stopped = b8::run_campaign(config, first, sha256, [&] { return stop; });
    check(stopped.state == "STOPPED" && stopped.completed_this_resume == 8U,
          "schema-v1 migration setup did not persist one batch");

    const auto results_path = directory / "candidate-results.tsv";
    const auto results_content = read_text(results_path);
    std::vector<b8::Candidate> completed{{3U, 66'411U}, {5U, 66'411U}};
    completed.insert(completed.end(), candidates.begin(), candidates.begin() + 8);
    const std::vector<b8::Candidate> remaining(candidates.begin() + 8, candidates.end());
    const auto checkpoint =
        "{\"campaign_id\":\"" + config.campaign_id +
        "\",\"candidate_results_hash\":\"" + hash_text(results_content, sha256) +
        "\",\"completed_set_sha256\":\"" + b8::candidate_set_sha256(completed, sha256) +
        "\",\"completed_unique_count\":\"10\",\"engine_binary_sha256\":\"" +
        config.engine_binary_sha256 + "\",\"engine_commit\":\"" + config.engine_commit +
        "\",\"last_batch_hash\":\"" + last_result_batch_hash(results_path) +
        "\",\"last_batch_id\":\"0\",\"next_batch_id\":\"1\",\"parent_campaign_id\":\"" +
        config.parent_campaign_id + "\",\"remaining_queue_sha256\":\"" +
        b8::candidate_set_sha256(remaining, sha256) +
        "\",\"schema_version\":\"1\",\"survivor_list_sha256\":\"" +
        config.survivor_list_sha256 +
        "\",\"terminal_status\":\"STOPPED\",\"updated_utc\":\"2026-08-22T00:00:00Z\"}\n";
    write_text(directory / "campaign.checkpoint.json", checkpoint);

    std::vector<std::size_t> sizes;
    const auto migrated = b8::run_campaign(config, deterministic_executor(&sizes), sha256);
    check(migrated.state == "COMPLETE_NO_PRIME" && migrated.completed_this_resume == 17U &&
              sizes == std::vector<std::size_t>({8U, 1U}) &&
              read_text(directory / "campaign.checkpoint.json").find("\"schema_version\":\"2\"") !=
                  std::string::npos,
          "schema-v1 checkpoint was not validated and migrated to schema v2");
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

void test_b32_stop_on_prime(const std::filesystem::path& root) {
    primeforge::PortableSha256Provider sha256;
    const auto directory = root / "b32-prime";
    auto config = config_for(directory, corpus(65U));
    config.batch_size = b8::max_batch_size;
    std::size_t calls = 0U;
    const b8::BatchExecutor executor = [&](const b8::BatchRequest& request, const b8::ResourceSink&,
                                           const b8::RuntimeIdsSink&, const b8::StopRequested&) {
        ++calls;
        return fake_execution(request, 31U);
    };
    const auto summary = b8::run_campaign(config, executor, sha256);
    check(summary.state == "PRIME_FOUND" && summary.prime_found && summary.completed_this_resume == 32U &&
              summary.remaining == 33U && calls == 1U,
          "B32 stop-on-prime did not retain the whole in-flight batch and stop before the next one");
    const auto rows = mathematical_rows(directory / "candidate-results.tsv");
    check(rows.size() == 32U && rows[31].find("PROVEN_PRIME") != std::string::npos,
          "B32 non-first-lane prime or peer composite results were lost");
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
        test_batch_dispatch();
        test_tail_and_checkpoint(root);
        test_b32_tail(root);
        test_stop_resume(root);
        test_b32_stop_resume(root);
        test_result_ahead_of_checkpoint_recovery(root);
        test_incomplete_appended_batch_recovery(root);
        test_schema_v1_checkpoint_migration(root);
        test_stop_on_prime(root);
        test_b32_stop_on_prime(root);
        test_telemetry_limits_and_truncation(root);
        std::filesystem::remove_all(root);
        std::cout << "primeforge-native-b8-tests: PASS\n"
                  << "covered=parser,transform-dispatch,B8+B8+B1,B32+B32+B1,atomic-checkpoint,write-ahead-recovery,incomplete-append-recovery,schema-v1-migration,B8-stop-resume,B32-stop-resume,B8-stop-on-prime,B32-stop-on-prime,telemetry-soft-hard,truncated-tail\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << "primeforge-native-b8-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
