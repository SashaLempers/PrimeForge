// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/mvp/flint_evidence_log.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace filesystem = std::filesystem;
namespace mvp = primeforge::mvp;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string{message});
    }
}

template <typename Action>
void check_throws(Action&& action, const std::string_view message) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, message);
}

[[nodiscard]] std::string read_file(const filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("cannot read test journal");
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void append_unchecked(const filesystem::path& path, const std::string_view bytes) {
    std::ofstream output{path, std::ios::binary | std::ios::app};
    if (!output) {
        throw std::runtime_error("cannot append test corruption");
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
        throw std::runtime_error("cannot flush test corruption");
    }
}

void overwrite_byte(
    const filesystem::path& path, const std::uint64_t offset, const char replacement) {
    std::fstream file{path, std::ios::binary | std::ios::in | std::ios::out};
    if (!file) {
        throw std::runtime_error("cannot open test journal for mutation");
    }
    file.seekp(static_cast<std::streamoff>(offset));
    file.put(replacement);
    file.flush();
    if (!file) {
        throw std::runtime_error("cannot write test mutation");
    }
}

[[nodiscard]] mvp::FlintEvidenceRecord binary_record() {
    return {std::string{"job-\0A", 6U}, std::string{"17\n", 3U},
            std::string{"prime\r\n\0", 8U}, std::string{"\xc3\xa9", 2U}};
}

void test_canonical_encoding() {
    const auto record = binary_record();
    const std::string expected =
        "{\"input_hex\":\"31370a\",\"job_id_hex\":\"6a6f622d0041\","
        "\"stderr_hex\":\"c3a9\",\"stdout_hex\":\"7072696d650d0a00\"}\n";
    check(mvp::canonical_flint_evidence_record(record) == expected,
          "canonical JSONL golden bytes");
    check(mvp::parse_canonical_flint_evidence_record(expected) == record,
          "binary fields round trip without loss");
    check_throws(
        [&] {
            auto uppercase = expected;
            uppercase[uppercase.find("31370a") + 5U] = 'A';
            static_cast<void>(mvp::parse_canonical_flint_evidence_record(uppercase));
        },
        "uppercase hexadecimal rejected");
    check_throws(
        [&] {
            auto crlf = expected;
            crlf.insert(crlf.size() - 1U, 1U, '\r');
            static_cast<void>(mvp::parse_canonical_flint_evidence_record(crlf));
        },
        "CRLF rejected");
    check_throws(
        [&] {
            static_cast<void>(mvp::parse_canonical_flint_evidence_record(expected + "x"));
        },
        "trailing bytes rejected");
}

void test_journal_recovery(const filesystem::path& directory) {
    const primeforge::PortableSha256Provider sha256;
    const auto path = directory / "flint-evidence.jsonl";
    mvp::FlintEvidenceLog log{path, sha256};
    check(filesystem::is_regular_file(path) && log.size() == 0U,
          "constructor durably creates an empty journal");

    const auto first = binary_record();
    const mvp::FlintEvidenceRecord second{"job-2", "19", "composite\n", ""};
    const auto first_bytes = mvp::canonical_flint_evidence_record(first);
    const auto second_bytes = mvp::canonical_flint_evidence_record(second);
    const auto first_slice = log.append(first);
    const auto second_slice = log.append(second);
    check(first_slice == mvp::FlintEvidenceSlice{0U, first_bytes.size()},
          "first append returns exact offset and length");
    check(second_slice == mvp::FlintEvidenceSlice{first_bytes.size(), second_bytes.size()},
          "second append is exactly contiguous");
    check(read_file(path) == first_bytes + second_bytes,
          "journal stores canonical LF-only bytes");
    check(log.read(first_slice) == first && log.read(second_slice) == second,
          "strict slice reads round trip");

    check_throws(
        [&] {
            static_cast<void>(log.read({first_slice.offset + 1U, first_slice.length - 1U}));
        },
        "slice beginning inside a record rejected");
    check_throws(
        [&] { static_cast<void>(log.read({first_slice.offset, first_slice.length - 1U})); },
        "slice ending inside a record rejected");
    check_throws(
        [&] {
            static_cast<void>(log.read(
                {first_slice.offset, first_slice.length + second_slice.length}));
        },
        "slice containing multiple records rejected");

    const std::vector<mvp::FlintEvidenceSlice> first_only{first_slice};
    const std::vector<mvp::FlintEvidenceSlice> all{first_slice, second_slice};
    const auto first_prefix = log.validate_contiguous_prefix(first_only);
    const auto complete_prefix = log.validate_complete_log(all);
    check(first_prefix.size == first_slice.length,
          "canonical partial prefix authenticated");
    check(complete_prefix.size == first_slice.length + second_slice.length,
          "complete journal size authenticated");
    check(complete_prefix.sha256.size() == 64U,
          "prefix authentication returns lowercase SHA-256");

    check_throws(
        [&] {
            const std::vector<mvp::FlintEvidenceSlice> reversed{second_slice, first_slice};
            static_cast<void>(log.validate_contiguous_prefix(reversed));
        },
        "out-of-order slices rejected");
    check_throws(
        [&] {
            const std::vector<mvp::FlintEvidenceSlice> gap{
                first_slice, {second_slice.offset + 1U, second_slice.length - 1U}};
            static_cast<void>(log.validate_contiguous_prefix(gap));
        },
        "slice gap rejected");
    check_throws(
        [&] {
            const std::vector<mvp::FlintEvidenceSlice> overlap{
                first_slice, {second_slice.offset - 1U, second_slice.length + 1U}};
            static_cast<void>(log.validate_contiguous_prefix(overlap));
        },
        "slice overlap rejected");

    append_unchecked(path, "{partial");
    const auto corrupted_size = log.size();
    check(corrupted_size > complete_prefix.size, "partial trailing record injected");
    check_throws([&] { static_cast<void>(log.validate_complete_log(all)); },
                 "unindexed trailing bytes rejected");
    check_throws([&] { static_cast<void>(log.authenticate_prefix(corrupted_size)); },
                 "partial trailing record rejected by strict prefix validation");
    check_throws([&] { static_cast<void>(log.append({"job-3", "23", "", ""})); },
                 "append refuses a pre-existing partial tail");

    auto wrong_digest = complete_prefix;
    wrong_digest.sha256.front() = wrong_digest.sha256.front() == '0' ? '1' : '0';
    check_throws([&] { log.truncate_authenticated(wrong_digest); },
                 "truncation with an incorrect digest rejected");
    check(log.size() == corrupted_size, "failed authentication never truncates");
    log.truncate_authenticated(complete_prefix);
    check(log.size() == complete_prefix.size && read_file(path) == first_bytes + second_bytes,
          "authenticated truncation removes only the corrupt tail");

    const auto input_digit_offset = first_bytes.find("31370a");
    check(input_digit_offset != std::string::npos, "mutation fixture offset found");
    overwrite_byte(path, input_digit_offset, '2');
    check(log.read(first_slice) != first,
          "canonical content mutation remains structurally readable");
    check_throws([&] { log.truncate_authenticated(complete_prefix); },
                 "content mutation detected by prefix authentication");
    overwrite_byte(path, input_digit_offset, '3');

    overwrite_byte(path, input_digit_offset + 1U, 'A');
    check_throws([&] { static_cast<void>(log.read(first_slice)); },
                 "noncanonical corruption detected during slice read");
    check_throws([&] { static_cast<void>(log.validate_complete_log(all)); },
                 "noncanonical corruption detected during complete validation");
    overwrite_byte(path, input_digit_offset + 1U, '1');
    check(log.validate_complete_log(all) == complete_prefix,
          "restored journal reproduces identical prefix authentication");

    check_throws(
        [&] {
            static_cast<void>(
                mvp::FlintEvidenceLog{directory / "missing" / "log.jsonl", sha256});
        },
        "constructor does not invent a missing parent directory");
}

void test_batch_append(const filesystem::path& directory) {
    const primeforge::PortableSha256Provider sha256;
    const auto path = directory / "flint-evidence-batch.jsonl";
    mvp::FlintEvidenceLog log{path, sha256};
    const std::vector<mvp::FlintEvidenceRecord> records{
        {"batch-0", "23", "prime\n", ""}, binary_record(),
        {"batch-2", "29", "", std::string{"diagnostic\0bytes", 16U}}};
    const auto slices = log.append_batch(records);
    check(slices.size() == records.size(), "batch returns one slice per input record");
    std::string expected;
    std::uint64_t expected_offset{};
    for (std::size_t index = 0U; index < records.size(); ++index) {
        const auto canonical = mvp::canonical_flint_evidence_record(records[index]);
        check(slices[index] == mvp::FlintEvidenceSlice{expected_offset, canonical.size()},
              "batch slice retains exact input order and extent");
        expected += canonical;
        expected_offset += static_cast<std::uint64_t>(canonical.size());
        check(log.read(slices[index]) == records[index], "batch slice round trip");
    }
    check(read_file(path) == expected, "batch is one canonical contiguous append");
    check(log.validate_complete_log(slices).size == expected.size(),
          "batch slices validate as the complete journal");
    mvp::FlintEvidenceLog reopened{path, sha256};
    check(read_file(path) == expected && reopened.validate_complete_log(slices).size == expected.size(),
          "reopening an existing journal never truncates or rewrites it");
    const auto before_empty = read_file(path);
    check(log.append_batch({}).empty(), "empty batch returns no slices");
    check(read_file(path) == before_empty, "empty batch performs no write");
}

} // namespace

int main() {
    try {
        test_canonical_encoding();
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto directory = filesystem::temp_directory_path() /
                               ("primeforge-flint-evidence-" + std::to_string(nonce));
        filesystem::create_directories(directory);
        try {
            test_journal_recovery(directory);
            test_batch_append(directory);
        } catch (...) {
            std::error_code ignored;
            filesystem::remove_all(directory, ignored);
            throw;
        }
        std::error_code ignored;
        filesystem::remove_all(directory, ignored);
        std::cout << "primeforge-flint-evidence-log-tests: PASS; canonical binary encoding, "
                     "single-commit batches, strict slices, contiguous order, corruption, trailing recovery, "
                     "authenticated truncation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-flint-evidence-log-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
