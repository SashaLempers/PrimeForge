// SPDX-License-Identifier: Apache-2.0

#include "primeforge/work/work_unit.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace primeforge::work {
namespace {

[[nodiscard]] std::string decimal(const std::uint64_t value) {
    return std::to_string(value);
}

void require_ascii_nfc_subset(const std::string_view value, const std::string_view field) {
    for (const unsigned char byte : value) {
        if (byte >= 0x80U) {
            throw std::invalid_argument(
                std::string{field} + " must use the stage-7 ASCII subset of NFC UTF-8");
        }
    }
}

[[nodiscard]] std::string quote_json(const std::string_view value) {
    require_ascii_nfc_subset(value, "canonical JSON string");
    static constexpr char hexadecimal[] = "0123456789abcdef";
    std::string result;
    result.push_back('"');
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\r':
                result += "\\r";
                break;
            default:
                if (byte < 0x20U) {
                    result += "\\u00";
                    result.push_back(hexadecimal[byte >> 4U]);
                    result.push_back(hexadecimal[byte & 0x0fU]);
                } else {
                    result.push_back(static_cast<char>(byte));
                }
        }
    }
    result.push_back('"');
    return result;
}

void validate_unit_fields(const WorkUnit& unit) {
    if (unit.family_id.empty()) {
        throw std::invalid_argument("family_id must not be empty");
    }
    require_ascii_nfc_subset(unit.family_id, "family_id");
    require_ascii_nfc_subset(unit.residue_compiler_version, "residue_compiler_version");
    require_ascii_nfc_subset(unit.proof_policy, "proof_policy");
    if (unit.interval.begin >= unit.interval.end) {
        throw std::invalid_argument("work-unit interval must be nonempty and half-open");
    }
    const auto definition_digest = sha256_from_hex(unit.canonical_definition_sha256);
    if (!definition_digest.has_value() ||
        sha256_to_hex(*definition_digest) != unit.canonical_definition_sha256) {
        throw std::invalid_argument("canonical_definition_sha256 must be lowercase SHA-256 hex");
    }
    if (unit.sieve_bounds.minimum_prime > unit.sieve_bounds.maximum_prime) {
        throw std::invalid_argument("sieve bounds are reversed");
    }
    if (!std::is_sorted(unit.constraints.begin(), unit.constraints.end()) ||
        std::adjacent_find(unit.constraints.begin(), unit.constraints.end()) != unit.constraints.end()) {
        throw std::invalid_argument("constraints must be sorted and unique");
    }
    for (const auto& constraint : unit.constraints) {
        require_ascii_nfc_subset(constraint, "constraint");
    }
}

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string_view text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

[[nodiscard]] std::string join_constraints(const std::vector<std::string>& constraints) {
    std::string result{"["};
    for (std::size_t index = 0U; index < constraints.size(); ++index) {
        if (index != 0U) {
            result.push_back(',');
        }
        result += quote_json(constraints[index]);
    }
    result.push_back(']');
    return result;
}

[[nodiscard]] std::string make_report(
    const std::uint64_t begin,
    const std::uint64_t end,
    const std::size_t unit_count,
    const std::size_t unique_count,
    const std::vector<std::string>& errors) {
    std::string errors_json{"["};
    for (std::size_t index = 0U; index < errors.size(); ++index) {
        if (index != 0U) {
            errors_json.push_back(',');
        }
        errors_json += quote_json(errors[index]);
    }
    errors_json.push_back(']');
    return "{\"errors\":" + errors_json +
           ",\"expected_begin\":" + quote_json(decimal(begin)) +
           ",\"expected_end\":" + quote_json(decimal(end)) +
           ",\"total_values\":" + quote_json(decimal(end - begin)) +
           ",\"unique_work_unit_ids\":" + quote_json(std::to_string(unique_count)) +
           ",\"valid\":" + (errors.empty() ? "true" : "false") +
           ",\"work_unit_count\":" + quote_json(std::to_string(unit_count)) + "}";
}

#if defined(_WIN32)
[[nodiscard]] std::string windows_error(const std::string_view operation) {
    return std::string{operation} + " failed with Windows error " + std::to_string(GetLastError());
}

void write_native(
    const std::filesystem::path& temporary,
    const std::string& content,
    const CheckpointFailurePoint failure_point) {
    const HANDLE handle = CreateFileW(
        temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(windows_error("CreateFileW"));
    }
    std::size_t offset = 0U;
    while (offset < content.size()) {
        const auto remaining = content.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, MAXDWORD));
        DWORD written = 0U;
        if (WriteFile(handle, content.data() + offset, chunk, &written, nullptr) == 0 ||
            written != chunk) {
            const auto message = windows_error("WriteFile");
            CloseHandle(handle);
            throw std::runtime_error(message);
        }
        offset += written;
    }
    if (failure_point == CheckpointFailurePoint::after_write_before_flush) {
        CloseHandle(handle);
        throw std::runtime_error("injected interruption after checkpoint write");
    }
    if (FlushFileBuffers(handle) == 0) {
        const auto message = windows_error("FlushFileBuffers");
        CloseHandle(handle);
        throw std::runtime_error(message);
    }
    if (CloseHandle(handle) == 0) {
        throw std::runtime_error(windows_error("CloseHandle"));
    }
    if (failure_point == CheckpointFailurePoint::after_flush_before_replace) {
        throw std::runtime_error("injected interruption after checkpoint flush");
    }
}

void replace_native(
    const std::filesystem::path& temporary, const std::filesystem::path& target) {
    if (MoveFileExW(
            temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        throw std::runtime_error(windows_error("MoveFileExW"));
    }
}
#else
void write_native(
    const std::filesystem::path& temporary,
    const std::string& content,
    const CheckpointFailurePoint failure_point) {
    const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(), "open checkpoint temporary");
    }
    std::size_t offset = 0U;
    while (offset < content.size()) {
        const auto written = ::write(descriptor, content.data() + offset, content.size() - offset);
        if (written < 0) {
            const auto error = errno;
            ::close(descriptor);
            throw std::system_error(error, std::generic_category(), "write checkpoint temporary");
        }
        offset += static_cast<std::size_t>(written);
    }
    if (failure_point == CheckpointFailurePoint::after_write_before_flush) {
        ::close(descriptor);
        throw std::runtime_error("injected interruption after checkpoint write");
    }
    if (::fsync(descriptor) != 0) {
        const auto error = errno;
        ::close(descriptor);
        throw std::system_error(error, std::generic_category(), "fsync checkpoint temporary");
    }
    if (::close(descriptor) != 0) {
        throw std::system_error(errno, std::generic_category(), "close checkpoint temporary");
    }
    if (failure_point == CheckpointFailurePoint::after_flush_before_replace) {
        throw std::runtime_error("injected interruption after checkpoint flush");
    }
}

void replace_native(
    const std::filesystem::path& temporary, const std::filesystem::path& target) {
    if (::rename(temporary.c_str(), target.c_str()) != 0) {
        throw std::system_error(errno, std::generic_category(), "rename checkpoint");
    }
    const auto parent = target.parent_path().empty() ? std::filesystem::path{"."} : target.parent_path();
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        static_cast<void>(::fsync(directory));
        static_cast<void>(::close(directory));
    }
}
#endif

}  // namespace

std::string canonical_work_unit_without_id(const WorkUnit& unit) {
    validate_unit_fields(unit);
    const auto seed_json = unit.seed.has_value() ? quote_json(decimal(*unit.seed)) : "null";
    return "{\"canonical_definition_sha256\":" + quote_json(unit.canonical_definition_sha256) +
           ",\"constraints\":" + join_constraints(unit.constraints) +
           ",\"family_id\":" + quote_json(unit.family_id) +
           ",\"interval\":{\"begin\":" + quote_json(decimal(unit.interval.begin)) +
           ",\"end\":" + quote_json(decimal(unit.interval.end)) + "}" +
           ",\"proof_policy\":" + quote_json(unit.proof_policy) +
           ",\"residue_compiler_version\":" + quote_json(unit.residue_compiler_version) +
           ",\"seed\":" + seed_json +
           ",\"sieve_bounds\":{\"maximum_prime\":" +
           quote_json(decimal(unit.sieve_bounds.maximum_prime)) +
           ",\"minimum_prime\":" + quote_json(decimal(unit.sieve_bounds.minimum_prime)) + "}}";
}

std::string canonical_work_unit(const WorkUnit& unit) {
    const auto without_id = canonical_work_unit_without_id(unit);
    if (unit.work_unit_id.empty()) {
        throw std::invalid_argument("work_unit_id must not be empty");
    }
    const auto id_digest = sha256_from_hex(unit.work_unit_id);
    if (!id_digest.has_value() || sha256_to_hex(*id_digest) != unit.work_unit_id) {
        throw std::invalid_argument("work_unit_id must be lowercase SHA-256 hex");
    }
    auto result = without_id;
    result.pop_back();
    result += ",\"work_unit_id\":" + quote_json(unit.work_unit_id) + "}";
    return result;
}

WorkUnit finalize_work_unit(WorkUnit unit, const Sha256Provider& sha256_provider) {
    std::sort(unit.constraints.begin(), unit.constraints.end());
    unit.constraints.erase(std::unique(unit.constraints.begin(), unit.constraints.end()), unit.constraints.end());
    unit.work_unit_id.clear();
    const auto canonical = canonical_work_unit_without_id(unit);
    unit.work_unit_id = sha256_to_hex(sha256_provider.digest(bytes_of(canonical)));
    return unit;
}

std::vector<WorkUnit> partition_work_units(
    const WorkUnit& prototype,
    const std::uint64_t domain_begin,
    const std::uint64_t domain_end,
    const std::uint64_t maximum_unit_span,
    const Sha256Provider& sha256_provider) {
    if (domain_begin > domain_end) {
        throw std::invalid_argument("partition domain is reversed");
    }
    if (maximum_unit_span == 0U) {
        throw std::invalid_argument("maximum_unit_span must be nonzero");
    }
    std::vector<WorkUnit> result;
    auto cursor = domain_begin;
    while (cursor < domain_end) {
        const auto remaining = domain_end - cursor;
        const auto span = std::min(maximum_unit_span, remaining);
        auto unit = prototype;
        unit.interval = {cursor, cursor + span};
        result.push_back(finalize_work_unit(std::move(unit), sha256_provider));
        cursor += span;
    }
    return result;
}

CoverageVerification verify_coverage(
    const std::uint64_t expected_begin,
    const std::uint64_t expected_end,
    const std::vector<WorkUnit>& units,
    const Sha256Provider& sha256_provider) {
    CoverageVerification result;
    if (expected_begin > expected_end) {
        result.errors.emplace_back("EXPECTED_DOMAIN_REVERSED");
        result.canonical_report_json = make_report(
            expected_begin, expected_begin, units.size(), 0U, result.errors);
        return result;
    }

    std::set<std::string> identifiers;
    std::vector<const WorkUnit*> ordered;
    ordered.reserve(units.size());
    std::string expected_family;
    std::string expected_definition;
    for (std::size_t index = 0U; index < units.size(); ++index) {
        const auto& unit = units[index];
        ordered.push_back(&unit);
        if (!identifiers.insert(unit.work_unit_id).second) {
            result.errors.push_back("DUPLICATE_ID:" + std::to_string(index));
        }
        try {
            const auto canonical = canonical_work_unit_without_id(unit);
            const auto recomputed = sha256_to_hex(sha256_provider.digest(bytes_of(canonical)));
            if (recomputed != unit.work_unit_id) {
                result.errors.push_back("CONTENT_HASH_MISMATCH:" + std::to_string(index));
            }
        } catch (const std::exception&) {
            result.errors.push_back("INVALID_UNIT:" + std::to_string(index));
        }
        if (index == 0U) {
            expected_family = unit.family_id;
            expected_definition = unit.canonical_definition_sha256;
        } else {
            if (unit.family_id != expected_family) {
                result.errors.push_back("FAMILY_MISMATCH:" + std::to_string(index));
            }
            if (unit.canonical_definition_sha256 != expected_definition) {
                result.errors.push_back("DEFINITION_MISMATCH:" + std::to_string(index));
            }
        }
    }

    std::sort(ordered.begin(), ordered.end(), [](const WorkUnit* left, const WorkUnit* right) {
        if (left->interval.begin != right->interval.begin) {
            return left->interval.begin < right->interval.begin;
        }
        return left->interval.end < right->interval.end;
    });
    auto cursor = expected_begin;
    for (std::size_t index = 0U; index < ordered.size(); ++index) {
        const auto interval = ordered[index]->interval;
        if (interval.begin > cursor) {
            result.errors.push_back("GAP_BEFORE:" + std::to_string(interval.begin));
        } else if (interval.begin < cursor) {
            result.errors.push_back("OVERLAP_AT:" + std::to_string(interval.begin));
        }
        if (interval.end <= interval.begin) {
            result.errors.push_back("EMPTY_OR_REVERSED_INTERVAL:" + std::to_string(index));
            continue;
        }
        cursor = std::max(cursor, interval.end);
    }
    if (ordered.empty() && expected_begin != expected_end) {
        result.errors.emplace_back("NO_UNITS_FOR_NONEMPTY_DOMAIN");
    } else if (cursor < expected_end) {
        result.errors.push_back("TRAILING_GAP_FROM:" + std::to_string(cursor));
    } else if (cursor > expected_end) {
        result.errors.push_back("DOMAIN_OVERRUN_TO:" + std::to_string(cursor));
    }

    result.valid = result.errors.empty();
    result.canonical_report_json = make_report(
        expected_begin, expected_end, units.size(), identifiers.size(), result.errors);
    return result;
}

void write_checkpoint_atomically(
    const std::filesystem::path& target,
    const std::string& canonical_content,
    const CheckpointFailurePoint failure_point) {
    if (target.empty() || target.filename().empty()) {
        throw std::invalid_argument("checkpoint target must name a file");
    }
    if (canonical_content.empty()) {
        throw std::invalid_argument("checkpoint content must not be empty");
    }
    const auto parent = target.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent)) {
        throw std::invalid_argument("checkpoint parent directory does not exist");
    }
    auto temporary = target;
    temporary += ".new";
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    write_native(temporary, canonical_content, failure_point);
    replace_native(temporary, target);
}

std::string read_checkpoint(const std::filesystem::path& target) {
    std::ifstream input(target, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open checkpoint: " + target.string());
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

}  // namespace primeforge::work
