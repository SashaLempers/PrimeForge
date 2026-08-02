// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace primeforge::cpu {

struct CpuSet {
    std::uint32_t id{};
    std::uint16_t group{};
    std::uint8_t logical_processor_index{};
    std::uint8_t core_index{};
    std::uint8_t last_level_cache_index{};
    std::uint8_t numa_node_index{};
    std::uint8_t efficiency_class{};
    std::uint8_t scheduling_class{};
    bool parked{};
    bool allocated{};
    bool allocated_to_target_process{};
};

struct CpuTopology {
    std::string source;
    std::vector<CpuSet> cpu_sets;
};

enum class AffinityStrategy {
    physical_core_spread,
    logical_processor_spread
};

[[nodiscard]] CpuTopology collect_topology();
[[nodiscard]] std::vector<CpuSet> build_affinity_plan(
    const std::vector<CpuSet>& cpu_sets,
    AffinityStrategy strategy,
    std::size_t maximum_workers);
[[nodiscard]] bool apply_current_thread_cpu_set(const CpuSet& cpu_set) noexcept;
void clear_current_thread_cpu_set() noexcept;

} // namespace primeforge::cpu
