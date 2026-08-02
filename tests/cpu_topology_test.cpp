// SPDX-License-Identifier: Apache-2.0

#include "primeforge/cpu/cpu_topology.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(const bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

[[nodiscard]] primeforge::cpu::CpuSet cpu_set(
    const std::uint32_t id,
    const std::uint8_t logical,
    const std::uint8_t core,
    const std::uint8_t cache) {
    primeforge::cpu::CpuSet result;
    result.id = id;
    result.logical_processor_index = logical;
    result.core_index = core;
    result.last_level_cache_index = cache;
    return result;
}

} // namespace

int main() {
    try {
        std::vector<primeforge::cpu::CpuSet> topology{
            cpu_set(100U, 0U, 0U, 0U), cpu_set(116U, 16U, 0U, 0U),
            cpu_set(101U, 1U, 1U, 0U), cpu_set(117U, 17U, 1U, 0U),
            cpu_set(108U, 8U, 2U, 1U), cpu_set(124U, 24U, 2U, 1U),
            cpu_set(109U, 9U, 3U, 1U), cpu_set(125U, 25U, 3U, 1U)};
        auto parked = cpu_set(999U, 31U, 9U, 2U);
        parked.parked = true;
        topology.push_back(parked);
        auto foreign = cpu_set(998U, 30U, 8U, 2U);
        foreign.allocated = true;
        topology.push_back(foreign);

        const auto physical = primeforge::cpu::build_affinity_plan(
            topology, primeforge::cpu::AffinityStrategy::physical_core_spread, 16U);
        check(physical.size() == 4U, "physical plan selects one processor per usable core");
        const std::vector<std::uint8_t> expected_physical{0U, 8U, 1U, 9U};
        for (std::size_t index = 0U; index < physical.size(); ++index) {
            check(physical[index].logical_processor_index == expected_physical[index],
                  "physical plan alternates last-level-cache domains");
        }

        const auto logical = primeforge::cpu::build_affinity_plan(
            topology, primeforge::cpu::AffinityStrategy::logical_processor_spread, 16U);
        const std::vector<std::uint8_t> expected_logical{0U, 8U, 1U, 9U, 16U, 24U, 17U, 25U};
        check(logical.size() == expected_logical.size(), "logical plan includes all usable SMT siblings");
        for (std::size_t index = 0U; index < logical.size(); ++index) {
            check(logical[index].logical_processor_index == expected_logical[index],
                  "logical plan fills physical cores before SMT siblings");
        }
        check(primeforge::cpu::build_affinity_plan(
                  topology, primeforge::cpu::AffinityStrategy::physical_core_spread, 2U).size() == 2U,
              "worker limit truncates affinity plan");

        const auto local = primeforge::cpu::collect_topology();
#ifdef _WIN32
        check(local.source == "GetSystemCpuSetInformation" && !local.cpu_sets.empty(),
              "Windows exposes real CPU-set topology");
#else
        check(local.source == "UNAVAILABLE", "portable CI reports unavailable topology honestly");
#endif
        std::cout << "primeforge-cpu-topology-tests: PASS\n";
        std::cout << "covered=physical-core spread, LLC interleave, SMT second pass, parked/foreign exclusion\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "primeforge-cpu-topology-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
