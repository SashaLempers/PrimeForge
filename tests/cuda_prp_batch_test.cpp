// SPDX-License-Identifier: Apache-2.0

#include "primeforge/adaptive_bound/adaptive_bound.hpp"
#include "primeforge/cuda/prp_batch.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(const bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    try {
        constexpr std::size_t vector_count = 8'192U;
        std::vector<std::uint64_t> values;
        values.reserve(vector_count);
        std::uint64_t state = 0x9e3779b97f4a7c15ULL;
        for (std::size_t index = 0U; index < vector_count; ++index) {
            state = state * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
            values.push_back(state | 1U);
        }
        values[0] = 0U;
        values[1] = 2U;
        values[2] = 2'047U;
        values[3] = 3'215'031'751ULL;
        values[4] = 18'446'744'073'709'551'557ULL;
        values[5] = 18'446'744'073'709'551'615ULL;

        auto cuda =
            primeforge::cuda_backend::make_cuda_base2_strong_prp_batch_backend(vector_count);
        std::vector<primeforge::prp::Base2StrongPrpVerdict> observed(values.size());
        cuda->test(values, observed);
        std::size_t probable_count = 0U;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const bool expected =
                primeforge::adaptive_bound::is_base2_strong_probable_prime_u64(values[index]);
            const bool actual =
                observed[index] == primeforge::prp::Base2StrongPrpVerdict::probable_prime;
            check(actual == expected,
                  "CUDA candidate-level PRP differs from CPU scalar oracle at " +
                      std::to_string(index));
            if (actual) ++probable_count;
        }

        std::cout << "cuda.prp.vectors=" << values.size() << '\n'
                  << "cuda.prp.probable=" << probable_count << '\n'
                  << "cuda.prp.scalar_agreement=YES\n"
                  << "cuda.prp.status=PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "CUDA PRP batch test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
