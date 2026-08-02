#include "primeforge/core/sha256.hpp"
#include "primeforge/core/status.hpp"
#include "primeforge/core/system_info.hpp"
#include "primeforge/engine/engine_adapter.hpp"

#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class FakeSha256Provider final : public primeforge::Sha256Provider {
public:
    [[nodiscard]] primeforge::Sha256Digest digest(std::span<const std::byte> bytes) const override {
        primeforge::Sha256Digest result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            const auto input = bytes.empty() ? 0U : std::to_integer<unsigned int>(bytes[index % bytes.size()]);
            result[index] = static_cast<std::byte>((input + static_cast<unsigned int>(index)) & 0xffU);
        }
        return result;
    }
};

class FakeEngineAdapter final : public primeforge::EngineAdapter {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "primeforge.fake.v1";
    }

    [[nodiscard]] primeforge::EngineCapabilities capabilities() const override {
        return {{"test-family"}, true, false, false};
    }

    [[nodiscard]] bool supports(const primeforge::EngineRequest& request) const noexcept override {
        return request.family_id == "test-family";
    }

    [[nodiscard]] primeforge::EngineResult run(const primeforge::EngineRequest& request) override {
        primeforge::EngineResult result{};
        result.status.primality = primeforge::PrimalityStatus::probable_prime;
        result.status.verification = primeforge::VerificationStatus::self_verified;
        result.status.novelty = primeforge::NoveltyStatus::not_checked;
        result.diagnostics = "fake result for " + request.job_id;
        result.raw_stdout_path = request.working_directory / "stdout.txt";
        result.raw_stderr_path = request.working_directory / "stderr.txt";
        return result;
    }
};

void test_status_axes() {
    primeforge::CandidateStatus status{};
    check(primeforge::to_string(status.primality) == "UNTESTED", "default primality status");
    check(primeforge::to_string(status.verification) == "UNVERIFIED", "default verification status");
    check(primeforge::to_string(status.novelty) == "NOT_CHECKED", "default novelty status");

    status.primality = primeforge::PrimalityStatus::probable_prime;
    check(status.verification == primeforge::VerificationStatus::unverified, "primality must not promote verification");
    check(status.novelty == primeforge::NoveltyStatus::not_checked, "primality must not promote novelty");
    check(primeforge::to_string(status.primality) == "PROBABLE_PRIME", "probable-prime spelling");
    check(primeforge::to_string(primeforge::PrimalityStatus::proven_prime) == "PROVEN_PRIME", "proven-prime spelling");
    check(primeforge::to_string(primeforge::VerificationStatus::independently_verified) == "INDEPENDENTLY_VERIFIED", "verification spelling");
    check(primeforge::to_string(primeforge::NoveltyStatus::due_diligence_complete) == "DUE_DILIGENCE_COMPLETE", "novelty spelling");
}

void test_sha256_abstraction() {
    const std::array<std::byte, 1> input{std::byte{0}};
    const FakeSha256Provider provider;
    const auto digest = provider.digest(input);
    const auto hex = primeforge::sha256_to_hex(digest);
    check(hex == "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "digest hex encoding");
    const auto decoded = primeforge::sha256_from_hex(hex);
    check(decoded.has_value() && *decoded == digest, "digest hex round trip");
    check(!primeforge::sha256_from_hex("invalid").has_value(), "invalid digest length rejected");
    check(!primeforge::sha256_from_hex(std::string(64U, 'z')).has_value(), "invalid digest character rejected");
}

void test_engine_adapter_contract() {
    FakeEngineAdapter adapter;
    const primeforge::EngineRequest supported{"job-1", "test-family", "{}", std::filesystem::path{"work"}};
    const primeforge::EngineRequest unsupported{"job-2", "other-family", "{}", std::filesystem::path{"work"}};
    check(adapter.id() == "primeforge.fake.v1", "stable adapter id");
    check(adapter.supports(supported), "supported family accepted");
    check(!adapter.supports(unsupported), "unsupported family rejected");
    const auto capabilities = adapter.capabilities();
    check(capabilities.supported_families.size() == 1U, "capability family count");
    const auto result = adapter.run(supported);
    check(result.status.primality == primeforge::PrimalityStatus::probable_prime, "adapter primality result");
    check(result.status.verification == primeforge::VerificationStatus::self_verified, "adapter verification result");
    check(result.status.novelty == primeforge::NoveltyStatus::not_checked, "adapter novelty result");
    check(result.status.primality != primeforge::PrimalityStatus::proven_prime, "PRP is not proof");
}

void test_system_information() {
    const auto info = primeforge::collect_system_info();
    check(info.compiler.name != "UNKNOWN", "compiler detected");
    check(info.compiler.cplusplus >= 202100L, "C++23 mode detected");
    check(info.operating_system.name != "UNKNOWN", "operating system detected");
    check(!info.operating_system.architecture.empty(), "architecture detected");
    check(info.cpu.logical_cores > 0U, "logical cores detected");
    check(info.electrical_power_watts == "UNKNOWN", "power remains unknown");
}

} // namespace

int main() {
    try {
        test_status_axes();
        test_sha256_abstraction();
        test_engine_adapter_contract();
        test_system_information();
        std::cout << "primeforge-tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
