// SPDX-License-Identifier: Apache-2.0

#include "primeforge/adaptive_bound/adaptive_bound.hpp"
#include "primeforge/sieve/sieve.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_failure(Function&& function, const std::string& message) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}

}  // namespace

int main() {
    try {
        namespace adaptive = primeforge::adaptive_bound;
        const std::vector<adaptive::BoundObservation> curve{
            {7U, 1'000U, 100U, 1U},
            {19U, 2'000U, 100U, 41U},
            {43U, 20'000U, 100U, 42U},
        };
        const auto offline = adaptive::select_offline_bound(curve, 100U);
        check(offline.upper_prime == 19U, "offline model uses measured eliminations");
        const auto online = adaptive::select_online_bound(curve, 100U);
        check(online.upper_prime == 19U, "online model stops at uneconomic block");
        check(curve[0].eliminated_count != curve[0].candidate_count / 7U,
              "fixture explicitly contradicts a 1/q assumption");

        const primeforge::benchmark::SummaryStatistics faster{
            10U, 20U, 15U, 1U, 10U, 20U};
        const primeforge::benchmark::SummaryStatistics slower{
            30U, 40U, 35U, 1U, 30U, 40U};
        const primeforge::benchmark::SummaryStatistics overlapping{
            15U, 35U, 25U, 2U, 15U, 35U};
        check(adaptive::compare_robustly(faster, slower) ==
                  adaptive::RobustComparison::better,
              "disjoint interval is robustly better");
        check(adaptive::compare_robustly(overlapping, slower) ==
                  adaptive::RobustComparison::inconclusive,
              "overlap is inconclusive");

        check(adaptive::is_base2_strong_probable_prime_u64(2U), "two is PRP");
        check(adaptive::is_base2_strong_probable_prime_u64(101U), "101 is PRP");
        check(!adaptive::is_base2_strong_probable_prime_u64(341U), "341 rejected");
        check(adaptive::is_base2_strong_probable_prime_u64(1'373'653U),
              "1373653 is an intentional base-2 strong pseudoprime");
        check(!primeforge::sieve::is_prime_u64(1'373'653U),
              "PRP result is not promoted to proven prime");

        expect_failure(
            [&] {
                const std::vector<adaptive::BoundObservation> invalid{
                    {7U, 1U, 10U, 5U}, {5U, 2U, 10U, 6U}};
                static_cast<void>(adaptive::select_offline_bound(
                    invalid, 1U));
            },
            "nonmonotonic bound curve rejected");

        std::cout << "adaptive_offline_bound=" << offline.upper_prime << '\n'
                  << "adaptive_online_bound=" << online.upper_prime << '\n'
                  << "prp_1373653_status=PROBABLE_PRIME\n"
                  << "proven_1373653_status=COMPOSITE\n"
                  << "PrimeForge adaptive-bound tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PrimeForge adaptive-bound tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
