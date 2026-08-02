// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/sha256.hpp"
#include "primeforge/work/work_unit.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string_view text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

void test_sha256_vectors() {
    const primeforge::PortableSha256Provider provider;
    check(primeforge::sha256_to_hex(provider.digest(bytes_of(""))) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 empty vector");
    check(primeforge::sha256_to_hex(provider.digest(bytes_of("abc"))) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 abc vector");
    check(primeforge::sha256_to_hex(provider.digest(bytes_of(
              "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256 multi-block vector");
}

[[nodiscard]] primeforge::work::WorkUnit prototype(
    const primeforge::Sha256Provider& provider) {
    primeforge::work::WorkUnit unit;
    unit.family_id = "family.test.v1";
    unit.canonical_definition_sha256 =
        primeforge::sha256_to_hex(provider.digest(bytes_of("family-definition-v1")));
    unit.constraints = {"n%3!=0", "k%2==1", "n%3!=0"};
    unit.residue_compiler_version = "primeforge-residue-v0";
    unit.sieve_bounds = {2U, 1'000'000U};
    unit.proof_policy = "PROVE_IF_SURVIVES";
    unit.seed = 20260802U;
    return unit;
}

void test_partition_and_identity() {
    const primeforge::PortableSha256Provider provider;
    const auto first = primeforge::work::partition_work_units(
        prototype(provider), 0U, 1'000U, 137U, provider);
    const auto second = primeforge::work::partition_work_units(
        prototype(provider), 0U, 1'000U, 137U, provider);
    check(first.size() == 8U && second.size() == first.size(), "deterministic unit count");
    check(first.front().canonical_definition_sha256 ==
              "09367f1d4242c68f3546995f3566c69fc5ff80cc6fcc8bd02c4368fcd905ce60",
          "independent definition-hash golden vector");
    check(first.front().work_unit_id ==
              "04c5b090dd357de83ec6f98eae169e7d2ae7c6f7dfbf56c08b8ab19203975e7a",
          "independent work-unit-id golden vector");
    check(primeforge::work::canonical_work_unit_without_id(first.front()) ==
              "{\"canonical_definition_sha256\":\"09367f1d4242c68f3546995f3566c69fc5ff80cc6fcc8bd02c4368fcd905ce60\","
              "\"constraints\":[\"k%2==1\",\"n%3!=0\"],\"family_id\":\"family.test.v1\","
              "\"interval\":{\"begin\":\"0\",\"end\":\"137\"},\"proof_policy\":\"PROVE_IF_SURVIVES\","
              "\"residue_compiler_version\":\"primeforge-residue-v0\",\"seed\":\"20260802\","
              "\"sieve_bounds\":{\"maximum_prime\":\"1000000\",\"minimum_prime\":\"2\"}}",
          "canonical work-unit golden bytes");
    std::vector<unsigned int> coverage(1'000U, 0U);
    for (std::size_t index = 0U; index < first.size(); ++index) {
        check(first[index].work_unit_id == second[index].work_unit_id, "deterministic work-unit id");
        check(first[index].interval == second[index].interval, "deterministic interval");
        check(first[index].work_unit_id.size() == 64U, "work-unit SHA-256 id width");
        const auto canonical = primeforge::work::canonical_work_unit(first[index]);
        check(canonical.find('\n') == std::string::npos, "canonical work unit has no newline");
        check(canonical.find(".0") == std::string::npos, "canonical work unit has no float");
        for (auto value = first[index].interval.begin; value < first[index].interval.end; ++value) {
            ++coverage[static_cast<std::size_t>(value)];
        }
    }
    check(std::all_of(coverage.begin(), coverage.end(), [](const unsigned int visits) {
              return visits == 1U;
          }),
          "exhaustive small-domain exact-once coverage");
    const auto verified = primeforge::work::verify_coverage(0U, 1'000U, first, provider);
    check(verified.valid && verified.errors.empty(), "valid partition accepted");
    check(verified.canonical_report_json.find("\"valid\":true") != std::string::npos,
          "coverage report validity");
}

void test_fault_detection() {
    const primeforge::PortableSha256Provider provider;
    const auto original = primeforge::work::partition_work_units(
        prototype(provider), 10U, 510U, 64U, provider);

    auto deleted = original;
    deleted.erase(deleted.begin() + 2);
    check(!primeforge::work::verify_coverage(10U, 510U, deleted, provider).valid,
          "deleted unit detected");

    auto duplicated = original;
    duplicated.push_back(original[3]);
    const auto duplicate_report = primeforge::work::verify_coverage(10U, 510U, duplicated, provider);
    check(!duplicate_report.valid && std::any_of(
              duplicate_report.errors.begin(), duplicate_report.errors.end(), [](const std::string& error) {
                  return error.starts_with("DUPLICATE_ID:");
              }),
          "duplicate id detected");

    auto corrupted = original;
    --corrupted[1].interval.end;
    const auto corruption_report = primeforge::work::verify_coverage(10U, 510U, corrupted, provider);
    check(!corruption_report.valid && std::any_of(
              corruption_report.errors.begin(), corruption_report.errors.end(), [](const std::string& error) {
                  return error.starts_with("CONTENT_HASH_MISMATCH:");
              }),
          "content corruption detected by id");

    auto reversed = prototype(provider);
    bool rejected = false;
    try {
        static_cast<void>(primeforge::work::partition_work_units(reversed, 2U, 1U, 10U, provider));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "reversed domain rejected");
}

void test_atomic_checkpoint_interruptions() {
    namespace filesystem = std::filesystem;
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto directory = filesystem::temp_directory_path() /
                           ("primeforge-stage7-" + std::to_string(nonce));
    filesystem::create_directories(directory);
    const auto checkpoint = directory / "checkpoint.json";
    try {
        primeforge::work::write_checkpoint_atomically(checkpoint, "{\"sequence\":\"1\"}");
        check(primeforge::work::read_checkpoint(checkpoint) == "{\"sequence\":\"1\"}",
              "initial checkpoint round trip");

        for (const auto point : {
                 primeforge::work::CheckpointFailurePoint::after_write_before_flush,
                 primeforge::work::CheckpointFailurePoint::after_flush_before_replace}) {
            bool interrupted = false;
            try {
                primeforge::work::write_checkpoint_atomically(
                    checkpoint, "{\"sequence\":\"2\"}", point);
            } catch (const std::runtime_error&) {
                interrupted = true;
            }
            check(interrupted, "checkpoint interruption injected");
            check(primeforge::work::read_checkpoint(checkpoint) == "{\"sequence\":\"1\"}",
                  "interruption preserves last durable checkpoint");
        }

        primeforge::work::write_checkpoint_atomically(checkpoint, "{\"sequence\":\"2\"}");
        check(primeforge::work::read_checkpoint(checkpoint) == "{\"sequence\":\"2\"}",
              "atomic replacement completes");
    } catch (...) {
        std::error_code ignored;
        filesystem::remove_all(directory, ignored);
        throw;
    }
    std::error_code ignored;
    filesystem::remove_all(directory, ignored);
}

}  // namespace

int main() {
    try {
        test_sha256_vectors();
        test_partition_and_identity();
        test_fault_detection();
        test_atomic_checkpoint_interruptions();
        std::cout << "primeforge-work-unit-tests: PASS; SHA-256 vectors, exact coverage, "
                     "deletion/duplication/corruption, atomic interruptions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-work-unit-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
