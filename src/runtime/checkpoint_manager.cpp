// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/checkpoint_manager.hpp"

#include "primeforge/work/work_unit.hpp"
#include "runtime_internal.hpp"

#include <charconv>
#include <span>
#include <stdexcept>
#include <string_view>

namespace primeforge::runtime {
namespace {

void validate(const CheckpointState& state) {
    if (state.campaign_id.empty()) {
        throw std::invalid_argument("checkpoint campaign id must not be empty");
    }
    if (state.progress_decimal.empty() ||
        (state.progress_decimal.size() > 1U && state.progress_decimal.front() == '0')) {
        throw std::invalid_argument("checkpoint progress must be canonical decimal");
    }
    for (const char character : state.progress_decimal) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument("checkpoint progress must be canonical decimal");
        }
    }
}

[[nodiscard]] std::string canonical_body(const CheckpointState& state) {
    validate(state);
    return "{\"campaign_id\":" + internal::json_escape(state.campaign_id) +
           ",\"opaque_payload\":" + internal::json_escape(state.opaque_payload) +
           ",\"progress_decimal\":" + internal::json_escape(state.progress_decimal) +
           ",\"sequence\":" + internal::json_escape(std::to_string(state.sequence)) + "}";
}

[[nodiscard]] std::string digest_hex(
    const Sha256Provider& provider,
    const std::string_view content) {
    const auto characters = std::span(content.data(), content.size());
    return sha256_to_hex(provider.digest(std::as_bytes(characters)));
}

[[nodiscard]] std::pair<std::string, std::size_t> parse_string(
    const std::string_view text,
    const std::size_t opening_quote) {
    if (opening_quote >= text.size() || text[opening_quote] != '"') {
        throw std::runtime_error("checkpoint string expected");
    }
    bool escaped = false;
    for (std::size_t index = opening_quote + 1U; index < text.size(); ++index) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (text[index] == '\\') {
            escaped = true;
            continue;
        }
        if (text[index] == '"') {
            return {
                internal::unescape_json_string(text.substr(opening_quote + 1U, index - opening_quote - 1U)),
                index + 1U};
        }
    }
    throw std::runtime_error("checkpoint string is truncated");
}

void consume(const std::string_view text, std::size_t& position, const std::string_view expected) {
    if (text.substr(position, expected.size()) != expected) {
        throw std::runtime_error("checkpoint canonical structure mismatch");
    }
    position += expected.size();
}

} // namespace

CheckpointManager::CheckpointManager(const Sha256Provider& sha256_provider)
    : sha256_provider_(sha256_provider) {}

void CheckpointManager::save(
    const std::filesystem::path& path,
    const CheckpointState& state) const {
    const std::string body = canonical_body(state);
    const std::string document = body.substr(0U, body.size() - 1U) +
                                 ",\"sha256\":" + internal::json_escape(digest_hex(sha256_provider_, body)) + "}";
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    work::write_checkpoint_atomically(path, document);
}

CheckpointState CheckpointManager::load(const std::filesystem::path& path) const {
    const std::string document = work::read_checkpoint(path);
    std::size_t position = 0U;
    CheckpointState state;
    consume(document, position, "{\"campaign_id\":");
    auto parsed = parse_string(document, position);
    state.campaign_id = std::move(parsed.first);
    position = parsed.second;
    consume(document, position, ",\"opaque_payload\":");
    parsed = parse_string(document, position);
    state.opaque_payload = std::move(parsed.first);
    position = parsed.second;
    consume(document, position, ",\"progress_decimal\":");
    parsed = parse_string(document, position);
    state.progress_decimal = std::move(parsed.first);
    position = parsed.second;
    consume(document, position, ",\"sequence\":");
    parsed = parse_string(document, position);
    const std::string sequence_text = std::move(parsed.first);
    position = parsed.second;
    consume(document, position, ",\"sha256\":");
    parsed = parse_string(document, position);
    const std::string expected_digest = std::move(parsed.first);
    position = parsed.second;
    consume(document, position, "}");
    if (position != document.size()) {
        throw std::runtime_error("checkpoint contains trailing data");
    }
    const auto converted = std::from_chars(
        sequence_text.data(), sequence_text.data() + sequence_text.size(), state.sequence);
    if (converted.ec != std::errc{} || converted.ptr != sequence_text.data() + sequence_text.size()) {
        throw std::runtime_error("checkpoint sequence is invalid");
    }
    const std::string body = canonical_body(state);
    if (digest_hex(sha256_provider_, body) != expected_digest) {
        throw std::runtime_error("checkpoint SHA-256 mismatch");
    }
    const std::string expected_document = body.substr(0U, body.size() - 1U) +
                                          ",\"sha256\":" + internal::json_escape(expected_digest) + "}";
    if (document != expected_document) {
        throw std::runtime_error("checkpoint is not canonical");
    }
    return state;
}

bool CheckpointManager::valid(const std::filesystem::path& path) const noexcept {
    try {
        static_cast<void>(load(path));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace primeforge::runtime
