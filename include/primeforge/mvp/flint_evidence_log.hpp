// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "primeforge/core/sha256.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace primeforge::mvp {

// The string members are byte containers. They may contain NULs, non-UTF-8
// process output, or any other byte value; the journal encodes them losslessly.
struct FlintEvidenceRecord {
    std::string job_id;
    std::string input;
    std::string stdout_bytes;
    std::string stderr_bytes;

    [[nodiscard]] bool operator==(const FlintEvidenceRecord&) const = default;
};

struct FlintEvidenceSlice {
    std::uint64_t offset{};
    std::uint64_t length{};

    [[nodiscard]] bool operator==(const FlintEvidenceSlice&) const = default;
};

struct FlintEvidencePrefix {
    std::uint64_t size{};
    std::string sha256;

    [[nodiscard]] bool operator==(const FlintEvidencePrefix&) const = default;
};

// Canonical records are UTF-8-compatible ASCII JSON lines with exactly one LF.
// Keys are byte-sorted and every value is lowercase hexadecimal, so arbitrary
// child-process bytes are preserved without locale or newline conversion.
[[nodiscard]] std::string canonical_flint_evidence_record(
    const FlintEvidenceRecord& record);

// Accepts only the exact canonical representation produced above, including
// its final LF. Any alternate key order, uppercase hex, CRLF, or trailing byte
// is rejected.
[[nodiscard]] FlintEvidenceRecord parse_canonical_flint_evidence_record(
    std::string_view canonical_line);

class FlintEvidenceLog {
public:
    // create_if_missing durably creates a new empty journal but leaves an
    // existing one untouched. open_existing is side-effect free and requires
    // a regular file; it is suitable for verification and recovery checks.
    enum class OpenMode {
        create_if_missing,
        open_existing
    };

    FlintEvidenceLog(
        std::filesystem::path path,
        const Sha256Provider& sha256_provider,
        OpenMode mode = OpenMode::create_if_missing);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] std::uint64_t size() const;

    // Appends, flushes, and asks the OS to commit the bytes before returning.
    // If an I/O error leaves a partial tail, truncate_authenticated() can safely
    // restore the last authenticated prefix.
    [[nodiscard]] FlintEvidenceSlice append(const FlintEvidenceRecord& record);

    // Serializes the complete batch in memory and performs one durable append
    // (one flush/OS commit) for the checkpoint transaction. Returned slices
    // retain the input order and address each exact record in the final log.
    [[nodiscard]] std::vector<FlintEvidenceSlice> append_batch(
        std::span<const FlintEvidenceRecord> records);

    // Reads exactly one slice. Both its beginning and end must be record
    // boundaries, and its contents must be canonical.
    [[nodiscard]] FlintEvidenceRecord read(const FlintEvidenceSlice& slice) const;

    // Authenticates a canonical prefix. A prefix may be shorter than the file,
    // which permits recovery from unauthenticated or corrupt trailing bytes.
    [[nodiscard]] FlintEvidencePrefix authenticate_prefix(std::uint64_t prefix_size) const;

    // Slices must start at zero, appear in order, and be exactly contiguous.
    [[nodiscard]] FlintEvidencePrefix validate_contiguous_prefix(
        std::span<const FlintEvidenceSlice> slices) const;

    // As above, but also rejects every trailing byte not described by slices.
    [[nodiscard]] FlintEvidencePrefix validate_complete_log(
        std::span<const FlintEvidenceSlice> slices) const;

    // Verifies both the canonical prefix and its SHA-256 before truncating.
    // The resulting file is flushed/committed before this function returns.
    void truncate_authenticated(const FlintEvidencePrefix& prefix);

private:
    std::filesystem::path path_;
    const Sha256Provider* sha256_provider_{};
};

} // namespace primeforge::mvp
