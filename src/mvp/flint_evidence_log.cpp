// SPDX-License-Identifier: Apache-2.0

#include "primeforge/mvp/flint_evidence_log.hpp"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace primeforge::mvp {
namespace {

constexpr std::string_view kInputPrefix{"{\"input_hex\":\""};
constexpr std::string_view kJobIdMarker{"\",\"job_id_hex\":\""};
constexpr std::string_view kStderrMarker{"\",\"stderr_hex\":\""};
constexpr std::string_view kStdoutMarker{"\",\"stdout_hex\":\""};
constexpr std::string_view kRecordSuffix{"\"}\n"};

[[nodiscard]] std::recursive_mutex& journal_io_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string_view text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] char hex_digit(const unsigned char nibble) noexcept {
    constexpr std::string_view digits{"0123456789abcdef"};
    return digits[nibble];
}

[[nodiscard]] std::string hex_encode(const std::string_view bytes) {
    if (bytes.size() > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::length_error("FLINT evidence field is too large");
    }
    std::string result;
    result.reserve(bytes.size() * 2U);
    for (const unsigned char byte : bytes) {
        result.push_back(hex_digit(static_cast<unsigned char>(byte >> 4U)));
        result.push_back(hex_digit(static_cast<unsigned char>(byte & 0x0fU)));
    }
    return result;
}

[[nodiscard]] unsigned char decode_nibble(const char digit) {
    if (digit >= '0' && digit <= '9') {
        return static_cast<unsigned char>(digit - '0');
    }
    if (digit >= 'a' && digit <= 'f') {
        return static_cast<unsigned char>(digit - 'a' + 10);
    }
    throw std::invalid_argument("FLINT evidence contains noncanonical hexadecimal");
}

[[nodiscard]] std::string hex_decode(const std::string_view hex) {
    if (hex.size() % 2U != 0U) {
        throw std::invalid_argument("FLINT evidence contains odd-length hexadecimal");
    }
    std::string result;
    result.reserve(hex.size() / 2U);
    for (std::size_t index = 0U; index < hex.size(); index += 2U) {
        const auto high = decode_nibble(hex[index]);
        const auto low = decode_nibble(hex[index + 1U]);
        result.push_back(static_cast<char>(static_cast<unsigned char>((high << 4U) | low)));
    }
    return result;
}

[[nodiscard]] std::string_view take_until(
    std::string_view& remaining, const std::string_view marker) {
    const auto position = remaining.find(marker);
    if (position == std::string_view::npos) {
        throw std::invalid_argument("FLINT evidence record has an invalid schema");
    }
    const auto value = remaining.substr(0U, position);
    remaining.remove_prefix(position + marker.size());
    return value;
}

void require_parent_directory(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent)) {
        throw std::runtime_error("FLINT evidence parent directory does not exist");
    }
}

[[nodiscard]] std::uint64_t file_size_or_zero(const std::filesystem::path& path) {
    std::error_code error;
    const auto exists = std::filesystem::exists(path, error);
    if (error) {
        throw std::system_error(error, "inspect FLINT evidence log");
    }
    if (!exists) {
        return 0U;
    }
    if (!std::filesystem::is_regular_file(path, error) || error) {
        if (error) {
            throw std::system_error(error, "inspect FLINT evidence log");
        }
        throw std::runtime_error("FLINT evidence path is not a regular file");
    }
    const auto result = std::filesystem::file_size(path, error);
    if (error) {
        throw std::system_error(error, "measure FLINT evidence log");
    }
    return result;
}

void require_streamable(const std::uint64_t value, const char* const field) {
    constexpr auto maximum = static_cast<std::uint64_t>(
        std::numeric_limits<std::streamoff>::max());
    if (value > maximum) {
        throw std::length_error(std::string{"FLINT evidence "} + field + " is too large");
    }
}

[[nodiscard]] std::string read_exact(
    const std::filesystem::path& path, const std::uint64_t offset, const std::uint64_t length) {
    require_streamable(offset, "offset");
    require_streamable(length, "slice");
    if (length > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("FLINT evidence slice does not fit in memory");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("cannot open FLINT evidence log for reading");
    }
    input.seekg(static_cast<std::streamoff>(offset));
    if (!input) {
        throw std::runtime_error("cannot seek in FLINT evidence log");
    }
    std::string bytes(static_cast<std::size_t>(length), '\0');
    if (length != 0U) {
        input.read(bytes.data(), static_cast<std::streamsize>(length));
        if (input.gcount() != static_cast<std::streamsize>(length)) {
            throw std::runtime_error("FLINT evidence log ended inside a slice");
        }
    }
    return bytes;
}

void require_record_boundary(
    const std::filesystem::path& path, const std::uint64_t offset) {
    if (offset == 0U) {
        return;
    }
    if (read_exact(path, offset - 1U, 1U) != "\n") {
        throw std::runtime_error("FLINT evidence slice does not start at a record boundary");
    }
}

void commit_append(std::FILE* const file) {
    if (std::fflush(file) != 0) {
        throw std::system_error(errno, std::generic_category(), "flush FLINT evidence append");
    }
#if defined(_WIN32)
    if (_commit(_fileno(file)) != 0) {
#else
    if (::fsync(fileno(file)) != 0) {
#endif
        throw std::system_error(errno, std::generic_category(), "commit FLINT evidence append");
    }
}

void close_file(std::FILE* const file, const char* const operation) {
    if (std::fclose(file) != 0) {
        throw std::system_error(errno, std::generic_category(), operation);
    }
}

void ensure_log_exists_durably(const std::filesystem::path& path) {
    require_parent_directory(path);
    std::error_code error;
    const bool existed = std::filesystem::exists(path, error);
    if (error) {
        throw std::system_error(error, "inspect FLINT evidence log before creation");
    }
#if defined(_WIN32)
    std::FILE* file{};
    if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || file == nullptr) {
        throw std::runtime_error("cannot create FLINT evidence log");
    }
#else
    std::FILE* file = std::fopen(path.c_str(), "ab");
    if (file == nullptr) {
        throw std::runtime_error("cannot create FLINT evidence log");
    }
#endif
    try {
        commit_append(file);
    } catch (...) {
        static_cast<void>(std::fclose(file));
        throw;
    }
    close_file(file, "close FLINT evidence creation");
#if !defined(_WIN32)
    if (!existed) {
        auto parent = path.parent_path();
        if (parent.empty()) {
            parent = ".";
        }
        const auto directory = ::open(parent.c_str(), O_RDONLY);
        if (directory < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "open FLINT evidence parent directory");
        }
        if (::fsync(directory) != 0) {
            const auto commit_error = errno;
            static_cast<void>(::close(directory));
            throw std::system_error(commit_error, std::generic_category(),
                                    "commit FLINT evidence parent directory");
        }
        if (::close(directory) != 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "close FLINT evidence parent directory");
        }
    }
#else
    static_cast<void>(existed);
#endif
}

void truncate_and_commit(const std::filesystem::path& path, const std::uint64_t size) {
#if defined(_WIN32)
    std::FILE* file{};
    if (_wfopen_s(&file, path.c_str(), L"r+b") != 0 || file == nullptr) {
        throw std::runtime_error("cannot open FLINT evidence log for truncation");
    }
    const auto descriptor = _fileno(file);
    const auto resize_error = _chsize_s(descriptor, size);
    if (resize_error != 0) {
        static_cast<void>(std::fclose(file));
        throw std::system_error(resize_error, std::generic_category(),
                                "truncate FLINT evidence log");
    }
    if (_commit(descriptor) != 0) {
        const auto error = errno;
        static_cast<void>(std::fclose(file));
        throw std::system_error(error, std::generic_category(), "commit FLINT evidence truncation");
    }
    close_file(file, "close FLINT evidence truncation");
#else
    if (size > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        throw std::length_error("FLINT evidence truncation offset is too large");
    }
    const auto descriptor = ::open(path.c_str(), O_RDWR);
    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "open FLINT evidence log for truncation");
    }
    if (::ftruncate(descriptor, static_cast<off_t>(size)) != 0) {
        const auto error = errno;
        static_cast<void>(::close(descriptor));
        throw std::system_error(error, std::generic_category(), "truncate FLINT evidence log");
    }
    if (::fsync(descriptor) != 0) {
        const auto error = errno;
        static_cast<void>(::close(descriptor));
        throw std::system_error(error, std::generic_category(),
                                "commit FLINT evidence truncation");
    }
    if (::close(descriptor) != 0) {
        throw std::system_error(errno, std::generic_category(), "close FLINT evidence truncation");
    }
#endif
}

} // namespace

std::string canonical_flint_evidence_record(const FlintEvidenceRecord& record) {
    return std::string{kInputPrefix} + hex_encode(record.input) + std::string{kJobIdMarker} +
           hex_encode(record.job_id) + std::string{kStderrMarker} + hex_encode(record.stderr_bytes) +
           std::string{kStdoutMarker} + hex_encode(record.stdout_bytes) + std::string{kRecordSuffix};
}

FlintEvidenceRecord parse_canonical_flint_evidence_record(
    const std::string_view canonical_line) {
    if (!canonical_line.starts_with(kInputPrefix) || !canonical_line.ends_with(kRecordSuffix)) {
        throw std::invalid_argument("FLINT evidence record is not canonical JSONL");
    }
    auto remaining = canonical_line;
    remaining.remove_prefix(kInputPrefix.size());
    const auto input_hex = take_until(remaining, kJobIdMarker);
    const auto job_id_hex = take_until(remaining, kStderrMarker);
    const auto stderr_hex = take_until(remaining, kStdoutMarker);
    if (!remaining.ends_with(kRecordSuffix)) {
        throw std::invalid_argument("FLINT evidence record has trailing bytes");
    }
    const auto stdout_hex = remaining.substr(0U, remaining.size() - kRecordSuffix.size());
    FlintEvidenceRecord record{hex_decode(job_id_hex), hex_decode(input_hex),
                               hex_decode(stdout_hex), hex_decode(stderr_hex)};
    if (canonical_flint_evidence_record(record) != canonical_line) {
        throw std::invalid_argument("FLINT evidence record has a noncanonical representation");
    }
    return record;
}

FlintEvidenceLog::FlintEvidenceLog(
    std::filesystem::path path, const Sha256Provider& sha256_provider)
    : path_{std::move(path)}, sha256_provider_{&sha256_provider} {
    if (path_.empty() || path_.filename().empty()) {
        throw std::invalid_argument("FLINT evidence log must name a file");
    }
    const std::scoped_lock lock{journal_io_mutex()};
    ensure_log_exists_durably(path_);
    static_cast<void>(file_size_or_zero(path_));
}

const std::filesystem::path& FlintEvidenceLog::path() const noexcept {
    return path_;
}

std::uint64_t FlintEvidenceLog::size() const {
    const std::scoped_lock lock{journal_io_mutex()};
    return file_size_or_zero(path_);
}

FlintEvidenceSlice FlintEvidenceLog::append(const FlintEvidenceRecord& record) {
    const auto slices = append_batch(std::span{&record, 1U});
    return slices.front();
}

std::vector<FlintEvidenceSlice> FlintEvidenceLog::append_batch(
    const std::span<const FlintEvidenceRecord> records) {
    if (records.empty()) {
        return {};
    }
    std::vector<std::string> canonical_records;
    canonical_records.reserve(records.size());
    std::size_t payload_size{};
    for (const auto& record : records) {
        auto canonical = canonical_flint_evidence_record(record);
        if (canonical.size() > std::numeric_limits<std::size_t>::max() - payload_size) {
            throw std::length_error("FLINT evidence batch is too large");
        }
        payload_size += canonical.size();
        canonical_records.push_back(std::move(canonical));
    }
    std::string payload;
    payload.reserve(payload_size);
    for (const auto& canonical : canonical_records) {
        payload += canonical;
    }
    const std::scoped_lock lock{journal_io_mutex()};
    require_parent_directory(path_);
    const auto offset = file_size_or_zero(path_);
    if (offset != 0U) {
        require_record_boundary(path_, offset);
    }
    if (payload_size > std::numeric_limits<std::uint64_t>::max() - offset) {
        throw std::length_error("FLINT evidence log offset overflow");
    }
    std::vector<FlintEvidenceSlice> slices;
    slices.reserve(canonical_records.size());
    auto next_offset = offset;
    for (const auto& canonical : canonical_records) {
        const auto length = static_cast<std::uint64_t>(canonical.size());
        slices.push_back({next_offset, length});
        next_offset += length;
    }
#if defined(_WIN32)
    std::FILE* file{};
    if (_wfopen_s(&file, path_.c_str(), L"ab") != 0 || file == nullptr) {
        throw std::runtime_error("cannot open FLINT evidence log for append");
    }
#else
    std::FILE* file = std::fopen(path_.c_str(), "ab");
    if (file == nullptr) {
        throw std::runtime_error("cannot open FLINT evidence log for append");
    }
#endif
    const auto written = std::fwrite(payload.data(), 1U, payload.size(), file);
    if (written != payload.size()) {
        const auto error = errno;
        static_cast<void>(std::fclose(file));
        throw std::system_error(error, std::generic_category(), "append FLINT evidence record");
    }
    try {
        commit_append(file);
    } catch (...) {
        static_cast<void>(std::fclose(file));
        throw;
    }
    close_file(file, "close FLINT evidence append");
    return slices;
}

FlintEvidenceRecord FlintEvidenceLog::read(const FlintEvidenceSlice& slice) const {
    const std::scoped_lock lock{journal_io_mutex()};
    const auto current_size = file_size_or_zero(path_);
    if (slice.length == 0U || slice.offset > current_size ||
        slice.length > current_size - slice.offset) {
        throw std::runtime_error("FLINT evidence slice is outside the log");
    }
    require_record_boundary(path_, slice.offset);
    return parse_canonical_flint_evidence_record(read_exact(path_, slice.offset, slice.length));
}

FlintEvidencePrefix FlintEvidenceLog::authenticate_prefix(const std::uint64_t prefix_size) const {
    const std::scoped_lock lock{journal_io_mutex()};
    const auto current_size = file_size_or_zero(path_);
    if (prefix_size > current_size) {
        throw std::runtime_error("FLINT evidence prefix is longer than the log");
    }
    const auto bytes = read_exact(path_, 0U, prefix_size);
    std::size_t begin{};
    while (begin < bytes.size()) {
        const auto newline = bytes.find('\n', begin);
        if (newline == std::string::npos) {
            throw std::runtime_error("FLINT evidence prefix ends inside a record");
        }
        const auto length = newline - begin + 1U;
        static_cast<void>(parse_canonical_flint_evidence_record(
            std::string_view{bytes}.substr(begin, length)));
        begin += length;
    }
    return {prefix_size, sha256_to_hex(sha256_provider_->digest(bytes_of(bytes)))};
}

FlintEvidencePrefix FlintEvidenceLog::validate_contiguous_prefix(
    const std::span<const FlintEvidenceSlice> slices) const {
    const std::scoped_lock lock{journal_io_mutex()};
    std::uint64_t expected_offset{};
    for (const auto& slice : slices) {
        if (slice.offset != expected_offset || slice.length == 0U ||
            slice.length > std::numeric_limits<std::uint64_t>::max() - expected_offset) {
            throw std::runtime_error("FLINT evidence slices are not ordered and contiguous");
        }
        static_cast<void>(read(slice));
        expected_offset += slice.length;
    }
    return authenticate_prefix(expected_offset);
}

FlintEvidencePrefix FlintEvidenceLog::validate_complete_log(
    const std::span<const FlintEvidenceSlice> slices) const {
    const std::scoped_lock lock{journal_io_mutex()};
    const auto prefix = validate_contiguous_prefix(slices);
    if (prefix.size != size()) {
        throw std::runtime_error("FLINT evidence log contains unindexed trailing bytes");
    }
    return prefix;
}

void FlintEvidenceLog::truncate_authenticated(const FlintEvidencePrefix& prefix) {
    const auto parsed_digest = sha256_from_hex(prefix.sha256);
    if (!parsed_digest.has_value() || sha256_to_hex(*parsed_digest) != prefix.sha256) {
        throw std::invalid_argument("FLINT evidence prefix SHA-256 is not canonical");
    }
    const std::scoped_lock lock{journal_io_mutex()};
    const auto current_size = file_size_or_zero(path_);
    if (prefix.size > current_size) {
        throw std::runtime_error("FLINT evidence truncation prefix is longer than the log");
    }
    const auto bytes = read_exact(path_, 0U, prefix.size);
    std::size_t begin{};
    while (begin < bytes.size()) {
        const auto newline = bytes.find('\n', begin);
        if (newline == std::string::npos) {
            throw std::runtime_error("FLINT evidence truncation prefix is not record-aligned");
        }
        const auto length = newline - begin + 1U;
        static_cast<void>(parse_canonical_flint_evidence_record(
            std::string_view{bytes}.substr(begin, length)));
        begin += length;
    }
    const auto actual_digest = sha256_to_hex(sha256_provider_->digest(bytes_of(bytes)));
    if (actual_digest != prefix.sha256) {
        throw std::runtime_error("FLINT evidence truncation prefix failed authentication");
    }
    if (prefix.size != current_size) {
        truncate_and_commit(path_, prefix.size);
    }
}

} // namespace primeforge::mvp
