// SPDX-License-Identifier: Apache-2.0

#include "primeforge/pipeline/modular_pipeline.hpp"

#include "primeforge/math/mul128.hpp"
#include "primeforge/runtime/benchmark_logger.hpp"
#include "primeforge/runtime/checkpoint_manager.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace primeforge::pipeline {
namespace {

constexpr std::string_view checkpoint_schema{
    "primeforge.modular.pipeline.checkpoint.v1"};

struct CheckpointPayload {
    std::uint64_t ledger_bytes{};
    std::string ledger_sha256;
    std::string tasks_sha256;
};

struct PendingBatch {
    std::size_t backend_index{};
    std::size_t begin{};
    std::size_t count{};
    std::future<std::vector<std::uint64_t>> future;
};

[[nodiscard]] std::uint64_t parse_u64(
    const std::string_view text,
    const std::string_view field) {
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        throw std::runtime_error(std::string{field} + " is not canonical decimal");
    }
    std::uint64_t value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::runtime_error(std::string{field} + " is not uint64 decimal");
    }
    return value;
}

[[nodiscard]] std::string digest_hex(
    const Sha256Provider& provider,
    const std::string_view bytes) {
    return sha256_to_hex(provider.digest(
        std::as_bytes(std::span(bytes.data(), bytes.size()))));
}

[[nodiscard]] std::string tasks_identity(
    const Sha256Provider& provider,
    const std::span<const cuda_backend::ModularMultiplyTask> tasks,
    const std::string_view backend_id) {
    std::string canonical = "primeforge.modular.pipeline.tasks.v1\nbackend=";
    canonical += backend_id;
    canonical += "\n";
    for (const auto& task : tasks) {
        canonical += std::to_string(task.left);
        canonical += ',';
        canonical += std::to_string(task.right);
        canonical += ',';
        canonical += std::to_string(task.modulus);
        canonical += '\n';
    }
    return digest_hex(provider, canonical);
}

[[nodiscard]] std::string make_payload(const CheckpointPayload& payload) {
    return "schema=" + std::string{checkpoint_schema} +
           ";ledger_bytes=" + std::to_string(payload.ledger_bytes) +
           ";ledger_sha256=" + payload.ledger_sha256 +
           ";tasks_sha256=" + payload.tasks_sha256;
}

[[nodiscard]] CheckpointPayload parse_payload(const std::string_view payload) {
    constexpr std::string_view schema_prefix{"schema="};
    constexpr std::string_view bytes_marker{";ledger_bytes="};
    constexpr std::string_view ledger_marker{";ledger_sha256="};
    constexpr std::string_view tasks_marker{";tasks_sha256="};
    if (!payload.starts_with(schema_prefix)) {
        throw std::runtime_error("pipeline checkpoint schema is missing");
    }
    const auto bytes_position = payload.find(bytes_marker);
    const auto ledger_position = payload.find(ledger_marker);
    const auto tasks_position = payload.find(tasks_marker);
    if (bytes_position == std::string_view::npos ||
        ledger_position == std::string_view::npos ||
        tasks_position == std::string_view::npos ||
        !(bytes_position < ledger_position && ledger_position < tasks_position)) {
        throw std::runtime_error("pipeline checkpoint payload structure mismatch");
    }
    if (payload.substr(schema_prefix.size(), bytes_position - schema_prefix.size()) !=
        checkpoint_schema) {
        throw std::runtime_error("pipeline checkpoint schema mismatch");
    }
    CheckpointPayload result;
    result.ledger_bytes = parse_u64(
        payload.substr(
            bytes_position + bytes_marker.size(),
            ledger_position - (bytes_position + bytes_marker.size())),
        "pipeline ledger bytes");
    result.ledger_sha256 = std::string{payload.substr(
        ledger_position + ledger_marker.size(),
        tasks_position - (ledger_position + ledger_marker.size()))};
    result.tasks_sha256 = std::string{payload.substr(tasks_position + tasks_marker.size())};
    if (!sha256_from_hex(result.ledger_sha256).has_value() ||
        !sha256_from_hex(result.tasks_sha256).has_value()) {
        throw std::runtime_error("pipeline checkpoint contains invalid SHA-256");
    }
    if (make_payload(result) != payload) {
        throw std::runtime_error("pipeline checkpoint payload is not canonical");
    }
    return result;
}

[[nodiscard]] std::string read_binary(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read pipeline result ledger");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::FILE* open_append(const std::filesystem::path& path) {
#ifdef _WIN32
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"ab") != 0) return nullptr;
    return file;
#else
    return std::fopen(path.c_str(), "ab");
#endif
}

void append_durably(const std::filesystem::path& path, const std::string_view bytes) {
    std::FILE* const file = open_append(path);
    if (file == nullptr) throw std::runtime_error("cannot open pipeline result ledger");
    const auto close_file = [&] { static_cast<void>(std::fclose(file)); };
    if (std::fwrite(bytes.data(), 1U, bytes.size(), file) != bytes.size()) {
        close_file();
        throw std::runtime_error("cannot append pipeline result ledger");
    }
    if (std::fflush(file) != 0) {
        close_file();
        throw std::runtime_error("cannot flush pipeline result ledger");
    }
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0) {
#else
    if (fsync(fileno(file)) != 0) {
#endif
        close_file();
        throw std::runtime_error("cannot durably flush pipeline result ledger");
    }
    if (std::fclose(file) != 0) {
        throw std::runtime_error("cannot close pipeline result ledger");
    }
}

[[nodiscard]] std::string ledger_record(
    const std::size_t index,
    const cuda_backend::ModularMultiplyTask& task,
    const std::uint64_t residue) {
    return "{\"index\":\"" + std::to_string(index) +
           "\",\"left\":\"" + std::to_string(task.left) +
           "\",\"modulus\":\"" + std::to_string(task.modulus) +
           "\",\"residue\":\"" + std::to_string(residue) +
           "\",\"right\":\"" + std::to_string(task.right) + "\"}\n";
}

void restore_ledger(
    const std::string_view content,
    const std::span<const cuda_backend::ModularMultiplyTask> tasks,
    const std::size_t committed,
    std::vector<std::uint64_t>& residues) {
    std::size_t position = 0U;
    for (std::size_t index = 0U; index < committed; ++index) {
        const auto newline = content.find('\n', position);
        if (newline == std::string_view::npos) {
            throw std::runtime_error("pipeline result ledger is truncated");
        }
        const auto line = content.substr(position, newline - position);
        const auto& task = tasks[index];
        const std::string prefix =
            "{\"index\":\"" + std::to_string(index) +
            "\",\"left\":\"" + std::to_string(task.left) +
            "\",\"modulus\":\"" + std::to_string(task.modulus) +
            "\",\"residue\":\"";
        const std::string suffix =
            "\",\"right\":\"" + std::to_string(task.right) + "\"}";
        if (!line.starts_with(prefix) || !line.ends_with(suffix) ||
            line.size() <= prefix.size() + suffix.size()) {
            throw std::runtime_error("pipeline result ledger record is not canonical");
        }
        const auto residue_text = line.substr(
            prefix.size(), line.size() - prefix.size() - suffix.size());
        const auto residue = parse_u64(residue_text, "pipeline residue");
        if (residue != math::multiply_mod_portable_reference(
                           task.left, task.right, task.modulus)) {
            throw std::runtime_error("pipeline durable residue fails CPU verification");
        }
        residues[index] = residue;
        position = newline + 1U;
    }
    if (position != content.size()) {
        throw std::runtime_error("pipeline result ledger has unexpected records");
    }
}

[[nodiscard]] std::string event_payload(
    const std::size_t begin,
    const std::size_t count,
    const std::size_t in_flight) {
    return "{\"begin\":\"" + std::to_string(begin) +
           "\",\"count\":\"" + std::to_string(count) +
           "\",\"in_flight\":\"" + std::to_string(in_flight) + "\"}";
}

}  // namespace

ModularBatchPipeline::ModularBatchPipeline(
    std::vector<std::unique_ptr<cuda_backend::ModularBatchBackend>> backends,
    const ModularPipelineOptions options,
    const Sha256Provider& sha256_provider)
    : backends_(std::move(backends)),
      options_(options),
      sha256_provider_(sha256_provider) {
    if (backends_.empty()) {
        throw std::invalid_argument("modular pipeline requires at least one backend");
    }
    if (options_.batch_size == 0U) {
        throw std::invalid_argument("modular pipeline batch size must be nonzero");
    }
    if (backends_.front() == nullptr || backends_.front()->id().empty()) {
        throw std::invalid_argument("modular pipeline backend is invalid");
    }
    backend_id_ = backends_.front()->id();
    for (const auto& backend : backends_) {
        if (backend == nullptr || backend->id() != backend_id_) {
            throw std::invalid_argument("modular pipeline backend ids differ");
        }
        if (backend->capacity() < options_.batch_size) {
            throw std::invalid_argument("modular pipeline batch exceeds backend capacity");
        }
    }
}

ModularPipelineResult ModularBatchPipeline::run(
    const std::span<const cuda_backend::ModularMultiplyTask> tasks,
    const std::filesystem::path& working_directory,
    const std::string& campaign_id,
    const std::stop_token stop_token) {
    if (campaign_id.empty()) throw std::invalid_argument("pipeline campaign id is empty");
    for (const auto& task : tasks) {
        if (task.modulus == 0U) throw std::invalid_argument("modulus must be nonzero");
    }
    std::filesystem::create_directories(working_directory);

    ModularPipelineResult result;
    result.residues.resize(tasks.size());
    result.checkpoint_path = working_directory / "pipeline.checkpoint.json";
    result.event_log_path = working_directory / "pipeline.events.jsonl";
    result.result_ledger_path = working_directory / "pipeline.results.jsonl";

    const auto identity = tasks_identity(sha256_provider_, tasks, backend_id_);
    runtime::CheckpointManager checkpoints{sha256_provider_};
    std::string ledger_content;
    std::size_t committed = 0U;

    if (std::filesystem::exists(result.checkpoint_path)) {
        const auto state = checkpoints.load(result.checkpoint_path);
        if (state.campaign_id != campaign_id ||
            state.sequence != parse_u64(state.progress_decimal, "pipeline progress") ||
            state.sequence > tasks.size()) {
            throw std::runtime_error("pipeline checkpoint campaign or progress mismatch");
        }
        const auto payload = parse_payload(state.opaque_payload);
        if (payload.tasks_sha256 != identity) {
            throw std::runtime_error("pipeline task identity mismatch");
        }
        auto durable_file = read_binary(result.result_ledger_path);
        if (durable_file.size() < payload.ledger_bytes) {
            throw std::runtime_error("pipeline result ledger is shorter than checkpoint");
        }
        if (durable_file.size() > payload.ledger_bytes) {
            std::filesystem::resize_file(result.result_ledger_path, payload.ledger_bytes);
            durable_file.resize(static_cast<std::size_t>(payload.ledger_bytes));
        }
        if (digest_hex(sha256_provider_, durable_file) != payload.ledger_sha256) {
            throw std::runtime_error("pipeline result ledger SHA-256 mismatch");
        }
        committed = static_cast<std::size_t>(state.sequence);
        ledger_content = std::move(durable_file);
        restore_ledger(ledger_content, tasks, committed, result.residues);
        result.resumed_tasks = committed;
    } else {
        if (std::filesystem::exists(result.result_ledger_path) &&
            std::filesystem::file_size(result.result_ledger_path) != 0U) {
            throw std::runtime_error("pipeline result ledger exists without checkpoint");
        }
        const CheckpointPayload payload{
            0U, digest_hex(sha256_provider_, std::string_view{}), identity};
        checkpoints.save(
            result.checkpoint_path,
            {campaign_id, make_payload(payload), "0", 0U});
    }

    runtime::BenchmarkLogger logger{result.event_log_path, campaign_id};
    logger.append(
        result.resumed_tasks == 0U ? "pipeline_start" : "pipeline_resume",
        "{\"backends\":\"" + std::to_string(backends_.size()) +
            "\",\"committed\":\"" + std::to_string(committed) +
            "\",\"total\":\"" + std::to_string(tasks.size()) + "\"}",
        runtime::utc_now());

    std::vector<std::size_t> available_backends;
    available_backends.reserve(backends_.size());
    for (std::size_t index = backends_.size(); index > 0U; --index) {
        available_backends.push_back(index - 1U);
    }
    std::deque<PendingBatch> pending;
    std::size_t next_to_submit = committed;
    bool stopping = stop_token.stop_requested();

    const auto save_progress = [&] {
        const CheckpointPayload payload{
            static_cast<std::uint64_t>(ledger_content.size()),
            digest_hex(sha256_provider_, ledger_content),
            identity};
        checkpoints.save(
            result.checkpoint_path,
            {campaign_id, make_payload(payload), std::to_string(committed),
             static_cast<std::uint64_t>(committed)});
    };

    try {
        while (committed < tasks.size()) {
            while (!stopping && next_to_submit < tasks.size() &&
                   !available_backends.empty()) {
                const auto backend_index = available_backends.back();
                available_backends.pop_back();
                const auto begin = next_to_submit;
                const auto count = std::min(options_.batch_size, tasks.size() - begin);
                std::vector<cuda_backend::ModularMultiplyTask> batch(
                    tasks.begin() + static_cast<std::ptrdiff_t>(begin),
                    tasks.begin() + static_cast<std::ptrdiff_t>(begin + count));
                auto* const backend = backends_[backend_index].get();
                pending.push_back({
                    backend_index,
                    begin,
                    count,
                    std::async(
                        std::launch::async,
                        [backend, batch = std::move(batch)]() mutable {
                            std::vector<std::uint64_t> residues(batch.size());
                            backend->multiply_mod(batch, residues);
                            return residues;
                        })});
                next_to_submit += count;
                ++result.submitted_batches;
                result.maximum_in_flight_batches =
                    std::max(result.maximum_in_flight_batches, pending.size());
                logger.append(
                    "batch_submitted",
                    event_payload(begin, count, pending.size()),
                    runtime::utc_now());
                if (stop_token.stop_requested()) stopping = true;
            }

            if (pending.empty()) break;
            auto batch = std::move(pending.front());
            pending.pop_front();
            auto observed = batch.future.get();
            if (observed.size() != batch.count) {
                throw std::runtime_error("pipeline backend returned wrong result count");
            }

            std::string durable_batch;
            for (std::size_t offset = 0U; offset < batch.count; ++offset) {
                const auto index = batch.begin + offset;
                const auto& task = tasks[index];
                const auto expected = math::multiply_mod_portable_reference(
                    task.left, task.right, task.modulus);
                if (observed[offset] != expected) {
                    throw std::runtime_error(
                        "pipeline CPU/GPU divergence at task " + std::to_string(index));
                }
                result.residues[index] = observed[offset];
                durable_batch += ledger_record(index, task, observed[offset]);
            }
            append_durably(result.result_ledger_path, durable_batch);
            ledger_content += durable_batch;
            committed += batch.count;
            save_progress();
            available_backends.push_back(batch.backend_index);
            logger.append(
                "batch_committed",
                event_payload(batch.begin, batch.count, pending.size()),
                runtime::utc_now());
            if (stop_token.stop_requested()) stopping = true;
        }
    } catch (...) {
        try {
            logger.append(
                "pipeline_failed",
                "{\"committed\":\"" + std::to_string(committed) + "\"}",
                runtime::utc_now());
        } catch (...) {
        }
        throw;
    }

    result.committed_tasks = committed;
    result.complete = committed == tasks.size();
    result.stopped = !result.complete && stopping;
    if (!result.complete) result.residues.resize(committed);
    logger.append(
        result.complete ? "pipeline_complete" : "pipeline_stopped",
        "{\"committed\":\"" + std::to_string(committed) +
            "\",\"total\":\"" + std::to_string(tasks.size()) + "\"}",
        runtime::utc_now());
    return result;
}

}  // namespace primeforge::pipeline
