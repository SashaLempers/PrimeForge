// SPDX-License-Identifier: Apache-2.0

#include "primeforge/cpu/cpu_topology.hpp"

#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

int main() {
    try {
        const auto topology = primeforge::cpu::collect_topology();
        std::set<std::pair<std::uint16_t, std::uint8_t>> cores;
        std::set<std::pair<std::uint16_t, std::uint8_t>> caches;
        for (const auto& cpu_set : topology.cpu_sets) {
            cores.emplace(cpu_set.group, cpu_set.core_index);
            caches.emplace(cpu_set.group, cpu_set.last_level_cache_index);
        }
        const auto physical = primeforge::cpu::build_affinity_plan(
            topology.cpu_sets, primeforge::cpu::AffinityStrategy::physical_core_spread,
            topology.cpu_sets.size());
        const auto logical = primeforge::cpu::build_affinity_plan(
            topology.cpu_sets, primeforge::cpu::AffinityStrategy::logical_processor_spread,
            topology.cpu_sets.size());
        std::cout << "cpu_topology.source=" << topology.source << '\n'
                  << "cpu_topology.cpu_sets=" << topology.cpu_sets.size() << '\n'
                  << "cpu_topology.physical_cores=" << cores.size() << '\n'
                  << "cpu_topology.last_level_cache_domains=" << caches.size() << '\n'
                  << "cpu_topology.physical_plan_workers=" << physical.size() << '\n'
                  << "cpu_topology.logical_plan_workers=" << logical.size() << '\n';
        for (std::size_t index = 0U; index < physical.size(); ++index) {
            const auto& target = physical[index];
            std::cout << "cpu_topology.physical_plan." << index << "=cpuset:" << target.id
                      << ",group:" << target.group
                      << ",logical:" << static_cast<unsigned int>(target.logical_processor_index)
                      << ",core:" << static_cast<unsigned int>(target.core_index)
                      << ",llc:" << static_cast<unsigned int>(target.last_level_cache_index) << '\n';
        }
        std::cout << "cpu_topology.status=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cpu_topology: FAIL: " << error.what() << '\n';
        return 1;
    }
}
