// SPDX-License-Identifier: Apache-2.0

#include "primeforge/cpu/cpu_topology.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace primeforge::cpu {
namespace {

using CoreKey = std::pair<std::uint16_t, std::uint8_t>;
using CacheKey = std::pair<std::uint16_t, std::uint8_t>;

[[nodiscard]] bool usable(const CpuSet& cpu_set) noexcept {
    return !cpu_set.parked && (!cpu_set.allocated || cpu_set.allocated_to_target_process);
}

[[nodiscard]] std::vector<CoreKey> physical_core_order(const std::vector<CpuSet>& cpu_sets) {
    std::map<CoreKey, CpuSet> representatives;
    for (const auto& cpu_set : cpu_sets) {
        if (!usable(cpu_set)) { continue; }
        const CoreKey key{cpu_set.group, cpu_set.core_index};
        const auto found = representatives.find(key);
        if (found == representatives.end() ||
            cpu_set.logical_processor_index < found->second.logical_processor_index) {
            representatives[key] = cpu_set;
        }
    }

    std::map<CacheKey, std::vector<CoreKey>> cache_domains;
    for (const auto& [key, representative] : representatives) {
        cache_domains[{representative.group, representative.last_level_cache_index}].push_back(key);
    }
    std::vector<CoreKey> result;
    for (std::size_t offset = 0U;; ++offset) {
        bool appended = false;
        for (const auto& [cache, cores] : cache_domains) {
            static_cast<void>(cache);
            if (offset < cores.size()) {
                result.push_back(cores[offset]);
                appended = true;
            }
        }
        if (!appended) { break; }
    }
    return result;
}

} // namespace

CpuTopology collect_topology() {
    CpuTopology result;
#ifdef _WIN32
    result.source = "GetSystemCpuSetInformation";
    ULONG required = 0U;
    static_cast<void>(GetSystemCpuSetInformation(nullptr, 0U, &required, GetCurrentProcess(), 0U));
    if (required == 0U) { return result; }
    std::vector<std::byte> buffer(required);
    if (GetSystemCpuSetInformation(
            reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()), required,
            &required, GetCurrentProcess(), 0U) == 0) {
        return result;
    }
    std::size_t offset = 0U;
    while (offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= required) {
        const auto* information = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
        if (information->Size == 0U || offset + information->Size > required) { break; }
        if (information->Type == CpuSetInformation) {
            const auto& value = information->CpuSet;
            result.cpu_sets.push_back({
                value.Id,
                value.Group,
                value.LogicalProcessorIndex,
                value.CoreIndex,
                value.LastLevelCacheIndex,
                value.NumaNodeIndex,
                value.EfficiencyClass,
                value.SchedulingClass,
                value.Parked != 0U,
                value.Allocated != 0U,
                value.AllocatedToTargetProcess != 0U});
        }
        offset += information->Size;
    }
#else
    result.source = "UNAVAILABLE";
#endif
    return result;
}

std::vector<CpuSet> build_affinity_plan(
    const std::vector<CpuSet>& cpu_sets,
    const AffinityStrategy strategy,
    const std::size_t maximum_workers) {
    if (maximum_workers == 0U) { return {}; }
    const auto core_order = physical_core_order(cpu_sets);
    std::map<CoreKey, std::vector<CpuSet>> members;
    for (const auto& cpu_set : cpu_sets) {
        if (usable(cpu_set)) {
            members[{cpu_set.group, cpu_set.core_index}].push_back(cpu_set);
        }
    }
    for (auto& [key, siblings] : members) {
        static_cast<void>(key);
        std::sort(siblings.begin(), siblings.end(), [](const CpuSet& left, const CpuSet& right) {
            return left.logical_processor_index < right.logical_processor_index;
        });
    }

    std::vector<CpuSet> result;
    if (strategy == AffinityStrategy::physical_core_spread) {
        for (const auto& core : core_order) {
            result.push_back(members.at(core).front());
            if (result.size() == maximum_workers) { break; }
        }
        return result;
    }

    std::size_t sibling = 0U;
    while (result.size() < maximum_workers) {
        bool appended = false;
        for (const auto& core : core_order) {
            const auto& processors = members.at(core);
            if (sibling < processors.size()) {
                result.push_back(processors[sibling]);
                appended = true;
                if (result.size() == maximum_workers) { break; }
            }
        }
        if (!appended) { break; }
        ++sibling;
    }
    return result;
}

bool apply_current_thread_cpu_set(const CpuSet& cpu_set) noexcept {
#ifdef _WIN32
    const ULONG id = cpu_set.id;
    return SetThreadSelectedCpuSets(GetCurrentThread(), &id, 1U) != 0;
#else
    static_cast<void>(cpu_set);
    return false;
#endif
}

void clear_current_thread_cpu_set() noexcept {
#ifdef _WIN32
    static_cast<void>(SetThreadSelectedCpuSets(GetCurrentThread(), nullptr, 0U));
#endif
}

} // namespace primeforge::cpu
