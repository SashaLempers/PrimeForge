// SPDX-License-Identifier: Apache-2.0

#include "primeforge/adaptive_bound/adaptive_bound.hpp"
#include "primeforge/prp/base2_batch.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(const bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function> void expect_failure(Function &&function, const std::string &message) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}

void verify_exact(primeforge::prp::Base2StrongPrpBatchBackend &backend,
                  const std::vector<std::uint64_t> &values) {
    std::vector<primeforge::prp::Base2StrongPrpVerdict> verdicts(values.size());
    backend.test(values, verdicts);
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const bool observed =
            verdicts[index] == primeforge::prp::Base2StrongPrpVerdict::probable_prime;
        const bool expected =
            primeforge::adaptive_bound::is_base2_strong_probable_prime_u64(values[index]);
        check(observed == expected, "batched CPU PRP differs from scalar oracle");
    }
}

}  // namespace

int main() {
    try {
        std::vector<std::uint64_t> values;
        values.reserve(20'000U);
        for (std::uint64_t value = 0U; value < 20'000U; ++value) {
            values.push_back(value);
        }
        for (const std::uint64_t value :
             {2'047ULL, 1'373'653ULL, 3'215'031'751ULL, 18'446'744'073'709'551'557ULL,
              18'446'744'073'709'551'615ULL}) {
            values.push_back(value);
        }

        auto cpu = primeforge::prp::make_cpu_base2_strong_prp_batch_backend(values.size(), 4U);
        check(cpu->capacity() == values.size() && !cpu->id().empty(),
              "CPU backend exposes stable identity and capacity");
        verify_exact(*cpu, values);

        std::vector<primeforge::prp::Base2StrongPrpVerdict> wrong_size(1U);
        expect_failure([&] { cpu->test(values, wrong_size); },
                       "CPU backend rejects mismatched output size");
        expect_failure(
            [] { static_cast<void>(primeforge::prp::make_cpu_base2_strong_prp_batch_backend(0U)); },
            "CPU backend rejects zero capacity");

        std::cout << "prp.cpu.vectors=" << values.size() << '\n'
                  << "prp.cpu.scalar_agreement=YES\n"
                  << "PrimeForge PRP batch tests: PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "PrimeForge PRP batch tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
