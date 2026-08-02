// SPDX-License-Identifier: Apache-2.0

#include "primeforge/family_sieve/family_sieve.hpp"

#include "primeforge/core/system_info.hpp"
#include "primeforge/cpu/cpu_topology.hpp"
#include "primeforge/sieve/sieve.hpp"
#include "vector_merge.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace primeforge::family_sieve {
namespace {

struct Counters {
    std::uint64_t rule_checks{};
    std::uint64_t modular_checks{};
    std::uint64_t exact_checks{};
};

struct WorkerOutput {
    std::vector<std::uint64_t> words;
    std::vector<std::uint64_t> list;
    Counters counters;
    bool pinning_applied{};
};

struct RuleColumns {
    std::vector<std::uint64_t> prime;
    std::vector<congruence::ExponentRuleScope> scope;
    std::vector<std::uint64_t> n_residue;
    std::vector<std::uint64_t> n_modulus;
    std::vector<std::int64_t> exact_n;
    std::vector<std::uint64_t> k_residue;
    std::vector<std::uint64_t> k_modulus;
};

[[nodiscard]] bool parity_matches(
    const std::int64_t value, const congruence::ParityConstraint constraint) noexcept {
    if (constraint == congruence::ParityConstraint::any) return true;
    const bool odd = (static_cast<std::uint64_t>(value) & 1U) != 0U;
    return constraint == congruence::ParityConstraint::odd ? odd : !odd;
}

[[nodiscard]] std::uint64_t canonical_index(
    const std::uint64_t k_index,
    const std::uint64_t n_index,
    const std::uint64_t n_count) noexcept {
    return k_index * n_count + n_index;
}

[[nodiscard]] std::pair<std::uint64_t, std::uint64_t> decode_index(
    const std::uint64_t traversal_index,
    const std::uint64_t k_count,
    const std::uint64_t n_count,
    const BitsetOrientation orientation) noexcept {
    if (orientation == BitsetOrientation::by_k) {
        return {traversal_index / n_count, traversal_index % n_count};
    }
    return {traversal_index % k_count, traversal_index / k_count};
}

[[nodiscard]] bool rule_matches(
    const congruence::ForbiddenRule& rule,
    const congruence::AffineExponentialFamily& family,
    const std::uint64_t k_index,
    const std::uint64_t n_index) {
    const auto n = family.n.value_at(n_index);
    bool exponent_matches = false;
    if (rule.exponent_scope == congruence::ExponentRuleScope::exact_exponent) {
        exponent_matches = n == rule.exact_exponent;
    } else if (rule.exponent_scope == congruence::ExponentRuleScope::positive_exponents) {
        exponent_matches = n > 0;
    } else {
        exponent_matches = n_index % rule.exponent_index_modulus ==
                           rule.exponent_index_residue;
    }
    return exponent_matches &&
           k_index % rule.k_index_modulus == rule.k_index_residue;
}

[[nodiscard]] bool columns_match(
    const RuleColumns& columns,
    const std::size_t index,
    const congruence::AffineExponentialFamily& family,
    const std::uint64_t k_index,
    const std::uint64_t n_index) {
    const auto n = family.n.value_at(n_index);
    bool exponent_matches = false;
    if (columns.scope[index] == congruence::ExponentRuleScope::exact_exponent) {
        exponent_matches = n == columns.exact_n[index];
    } else if (columns.scope[index] == congruence::ExponentRuleScope::positive_exponents) {
        exponent_matches = n > 0;
    } else {
        exponent_matches = n_index % columns.n_modulus[index] == columns.n_residue[index];
    }
    return exponent_matches && k_index % columns.k_modulus[index] == columns.k_residue[index];
}

[[nodiscard]] bool candidate_is_valid(
    const congruence::AffineExponentialFamily& family,
    const std::uint64_t k_index,
    const std::uint64_t n_index) {
    return parity_matches(family.k.value_at(k_index), family.k_parity) &&
           parity_matches(family.n.value_at(n_index), family.n_parity);
}

[[nodiscard]] bool proper_factor(
    const congruence::AffineExponentialFamily& family,
    const std::uint64_t k_index,
    const std::uint64_t n_index,
    const std::uint64_t prime,
    Counters& counters) {
    ++counters.modular_checks;
    if (congruence::evaluate_modulo(family, k_index, n_index, prime) != 0U) return false;
    ++counters.exact_checks;
    const auto value = congruence::evaluate_exact(family, k_index, n_index);
    if (value <= math::BigInteger{1} || value.modulo(prime) != 0U) return false;
    const auto absolute = value.is_negative() ? -value : value;
    return absolute > math::BigInteger::from_decimal(std::to_string(prime));
}

void record_elimination(
    WorkerOutput& output,
    const CandidateStorage storage,
    const std::uint64_t index) {
    if (storage == CandidateStorage::list) {
        output.list.push_back(index);
    } else {
        output.words[static_cast<std::size_t>(index / 64U)] |=
            std::uint64_t{1} << (index % 64U);
    }
}

[[nodiscard]] bool word_contains(
    const std::vector<std::uint64_t>& words, const std::uint64_t index) noexcept {
    return (words[static_cast<std::size_t>(index / 64U)] &
            (std::uint64_t{1} << (index % 64U))) != 0U;
}

[[nodiscard]] std::vector<congruence::ForbiddenRule> flatten_rules(
    const congruence::CompiledTable& table) {
    std::vector<congruence::ForbiddenRule> result;
    for (const auto& prime_table : table.prime_tables) {
        result.insert(result.end(), prime_table.rules.begin(), prime_table.rules.end());
    }
    return result;
}

[[nodiscard]] RuleColumns make_columns(
    const std::vector<congruence::ForbiddenRule>& rules) {
    RuleColumns result;
    const auto count = rules.size();
    result.prime.reserve(count);
    result.scope.reserve(count);
    result.n_residue.reserve(count);
    result.n_modulus.reserve(count);
    result.exact_n.reserve(count);
    result.k_residue.reserve(count);
    result.k_modulus.reserve(count);
    for (const auto& rule : rules) {
        result.prime.push_back(rule.prime);
        result.scope.push_back(rule.exponent_scope);
        result.n_residue.push_back(rule.exponent_index_residue);
        result.n_modulus.push_back(rule.exponent_index_modulus);
        result.exact_n.push_back(rule.exact_exponent);
        result.k_residue.push_back(rule.k_index_residue);
        result.k_modulus.push_back(rule.k_index_modulus);
    }
    return result;
}

void prefetch_address(const void* const address) noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_prefetch(static_cast<const char*>(address), _MM_HINT_T0);
#elif (defined(__GNUC__) || defined(__clang__))
    __builtin_prefetch(address, 0, 3);
#else
    static_cast<void>(address);
#endif
}

void scan_segment(
    const congruence::CompiledTable& table,
    const std::vector<congruence::ForbiddenRule>& rules,
    const RuleColumns& columns,
    const Options& options,
    const std::vector<std::uint64_t>& premarked,
    const std::uint64_t traversal_begin,
    const std::uint64_t traversal_end,
    WorkerOutput& output) {
    const auto k_count = table.family.k.size();
    const auto n_count = table.family.n.size();
    const auto inspect = [&](const std::uint64_t traversal_index,
                             const std::uint64_t prime,
                             const bool matches) {
        ++output.counters.rule_checks;
        if (!matches) return;
        const auto [k_index, n_index] = decode_index(
            traversal_index, k_count, n_count, options.orientation);
        if (!candidate_is_valid(table.family, k_index, n_index)) return;
        const auto index = canonical_index(k_index, n_index, n_count);
        if (word_contains(premarked, index)) return;
        if (proper_factor(table.family, k_index, n_index, prime, output.counters)) {
            record_elimination(output, options.storage, index);
        }
    };

    if (!options.compressed_classes) {
        if (options.loop_order == LoopOrder::prime_major) {
            for (const auto prime : table.primes) {
                for (auto traversal = traversal_begin; traversal < traversal_end; ++traversal) {
                    const auto [k_index, n_index] = decode_index(
                        traversal, k_count, n_count, options.orientation);
                    inspect(traversal, prime, candidate_is_valid(table.family, k_index, n_index));
                }
            }
        } else {
            for (auto traversal = traversal_begin; traversal < traversal_end; ++traversal) {
                const auto [k_index, n_index] = decode_index(
                    traversal, k_count, n_count, options.orientation);
                if (!candidate_is_valid(table.family, k_index, n_index)) continue;
                for (const auto prime : table.primes) inspect(traversal, prime, true);
            }
        }
        return;
    }

    const auto check_aos = [&](const std::uint64_t traversal, const std::size_t rule_index) {
        if (options.explicit_prefetch && rule_index + 4U < rules.size()) {
            prefetch_address(&rules[rule_index + 4U]);
        }
        const auto [k_index, n_index] = decode_index(
            traversal, k_count, n_count, options.orientation);
        inspect(
            traversal,
            rules[rule_index].prime,
            rule_matches(rules[rule_index], table.family, k_index, n_index));
    };
    const auto check_soa = [&](const std::uint64_t traversal, const std::size_t rule_index) {
        if (options.explicit_prefetch && rule_index + 8U < columns.prime.size()) {
            prefetch_address(&columns.prime[rule_index + 8U]);
        }
        const auto [k_index, n_index] = decode_index(
            traversal, k_count, n_count, options.orientation);
        inspect(
            traversal,
            columns.prime[rule_index],
            columns_match(columns, rule_index, table.family, k_index, n_index));
    };
    const auto check = [&](const std::uint64_t traversal, const std::size_t rule_index) {
        if (options.metadata_layout == MetadataLayout::array_of_structures) {
            check_aos(traversal, rule_index);
        } else {
            check_soa(traversal, rule_index);
        }
    };

    if (options.loop_order == LoopOrder::prime_major) {
        for (std::size_t rule_index = 0U; rule_index < rules.size(); ++rule_index) {
            for (auto traversal = traversal_begin; traversal < traversal_end; ++traversal) {
                check(traversal, rule_index);
            }
        }
    } else {
        for (auto traversal = traversal_begin; traversal < traversal_end; ++traversal) {
            for (std::size_t rule_index = 0U; rule_index < rules.size(); ++rule_index) {
                check(traversal, rule_index);
            }
        }
    }
}

[[nodiscard]] bool probe_huge_pages() noexcept {
#if defined(_WIN32)
    const auto size = GetLargePageMinimum();
    if (size == 0U) return false;
    void* const memory = VirtualAlloc(
        nullptr, size, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    if (memory == nullptr) return false;
    static_cast<void>(VirtualFree(memory, 0U, MEM_RELEASE));
    return true;
#else
    return false;
#endif
}

[[nodiscard]] std::uint64_t checked_candidate_count(
    const congruence::AffineExponentialFamily& family) {
    const auto k_count = family.k.size();
    const auto n_count = family.n.size();
    if (n_count != 0U && k_count > std::numeric_limits<std::uint64_t>::max() / n_count) {
        throw std::length_error("family sieve candidate count overflows");
    }
    return k_count * n_count;
}

void premark_small_prime_union(
    const congruence::CompiledTable& table,
    const std::size_t prime_count,
    std::vector<std::uint64_t>& words,
    Counters& counters) {
    const auto limit = std::min(prime_count, table.prime_tables.size());
    const auto k_count = table.family.k.size();
    const auto n_count = table.family.n.size();
    for (std::size_t table_index = 0U; table_index < limit; ++table_index) {
        for (const auto& rule : table.prime_tables[table_index].rules) {
            for (std::uint64_t k_index = 0U; k_index < k_count; ++k_index) {
                for (std::uint64_t n_index = 0U; n_index < n_count; ++n_index) {
                    ++counters.rule_checks;
                    if (!candidate_is_valid(table.family, k_index, n_index) ||
                        !rule_matches(rule, table.family, k_index, n_index)) {
                        continue;
                    }
                    const auto index = canonical_index(k_index, n_index, n_count);
                    if (!word_contains(words, index) &&
                        proper_factor(table.family, k_index, n_index, rule.prime, counters)) {
                        words[static_cast<std::size_t>(index / 64U)] |=
                            std::uint64_t{1} << (index % 64U);
                    }
                }
            }
        }
    }
}

[[nodiscard]] bool premark_crt_template(
    const congruence::CompiledTable& table,
    const Options& options,
    std::vector<std::uint64_t>& words,
    Counters& counters) {
    const auto limit = std::min(options.crt_prime_count, table.prime_tables.size());
    if (limit < 2U) return false;
    std::uint64_t k_period = 1U;
    std::uint64_t n_period = 1U;
    const auto bounded_lcm = [](const std::uint64_t left, const std::uint64_t right) {
        const auto divisor = std::gcd(left, right);
        if (left / divisor > std::numeric_limits<std::uint64_t>::max() / right) {
            return std::uint64_t{0};
        }
        return left / divisor * right;
    };
    for (std::size_t table_index = 0U; table_index < limit; ++table_index) {
        for (const auto& rule : table.prime_tables[table_index].rules) {
            if (rule.exponent_scope != congruence::ExponentRuleScope::index_congruence) continue;
            k_period = bounded_lcm(k_period, rule.k_index_modulus);
            n_period = bounded_lcm(n_period, rule.exponent_index_modulus);
            if (k_period == 0U || n_period == 0U ||
                k_period > options.crt_memory_limit_bytes / sizeof(std::uint64_t) / n_period) {
                return false;
            }
        }
    }
    const auto cells = k_period * n_period;
    if (cells == 0U || cells * sizeof(std::uint64_t) > options.crt_memory_limit_bytes) return false;
    std::vector<std::uint64_t> factor(static_cast<std::size_t>(cells), 0U);
    for (std::size_t table_index = 0U; table_index < limit; ++table_index) {
        for (const auto& rule : table.prime_tables[table_index].rules) {
            if (rule.exponent_scope != congruence::ExponentRuleScope::index_congruence) continue;
            for (std::uint64_t k = rule.k_index_residue; k < k_period; k += rule.k_index_modulus) {
                for (std::uint64_t n = rule.exponent_index_residue; n < n_period;
                     n += rule.exponent_index_modulus) {
                    auto& cell = factor[static_cast<std::size_t>(k * n_period + n)];
                    if (cell == 0U) cell = rule.prime;
                }
            }
        }
    }
    const auto k_count = table.family.k.size();
    const auto n_count = table.family.n.size();
    for (std::uint64_t k = 0U; k < k_count; ++k) {
        for (std::uint64_t n = 0U; n < n_count; ++n) {
            const auto prime = factor[static_cast<std::size_t>(
                (k % k_period) * n_period + (n % n_period))];
            ++counters.rule_checks;
            if (prime == 0U || !candidate_is_valid(table.family, k, n)) continue;
            const auto index = canonical_index(k, n, n_count);
            if (proper_factor(table.family, k, n, prime, counters)) {
                words[static_cast<std::size_t>(index / 64U)] |=
                    std::uint64_t{1} << (index % 64U);
            }
        }
    }
    return true;
}

[[nodiscard]] const char* storage_text(const CandidateStorage value) noexcept {
    return value == CandidateStorage::list ? "list" : "dense-bitset";
}

}  // namespace

Result run(
    const congruence::CompiledTable& table,
    const Sha256Provider& sha256,
    const Options& options) {
    const auto validation = congruence::validate_compiled_table(table, sha256);
    if (!validation.valid) throw std::invalid_argument("family sieve requires a valid table");
    if (options.threads == 0U || options.segment_candidates == 0U) {
        throw std::invalid_argument("threads and segment size must be nonzero");
    }
    const auto candidate_count = checked_candidate_count(table.family);
    const auto word_count = static_cast<std::size_t>((candidate_count + 63U) / 64U);
    const auto rules = flatten_rules(table);
    const auto columns = make_columns(rules);
    std::vector<std::uint64_t> premarked(word_count, 0U);
    Counters premark_counters{};
    bool crt_applied = false;
    if (options.crt_prime_count != 0U) {
        crt_applied = premark_crt_template(
            table, options, premarked, premark_counters);
    }
    if (options.wheel_prime_count != 0U) {
        premark_small_prime_union(
            table, options.wheel_prime_count, premarked, premark_counters);
    }

    const auto segment_count = (candidate_count + options.segment_candidates - 1U) /
                               options.segment_candidates;
    const auto thread_count = std::max(1U, std::min<unsigned int>(
        options.threads, static_cast<unsigned int>(std::min<std::uint64_t>(
                             segment_count, std::numeric_limits<unsigned int>::max()))));
    std::vector<cpu::CpuSet> affinity_plan;
    if (options.thread_placement != ThreadPlacement::scheduler_managed) {
        const auto topology = cpu::collect_topology();
        const auto strategy = options.thread_placement == ThreadPlacement::physical_core_spread
                                  ? cpu::AffinityStrategy::physical_core_spread
                                  : cpu::AffinityStrategy::logical_processor_spread;
        affinity_plan = cpu::build_affinity_plan(topology.cpu_sets, strategy, thread_count);
    }
    std::vector<WorkerOutput> outputs(thread_count);
    for (auto& output : outputs) {
        if (options.storage == CandidateStorage::dense_bitset) {
            output.words.assign(word_count, 0U);
        }
    }
    std::atomic<std::uint64_t> next_segment{};
    std::vector<std::exception_ptr> failures(thread_count);
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (unsigned int worker = 0U; worker < thread_count; ++worker) {
        workers.emplace_back([&, worker] {
            if (worker < affinity_plan.size()) {
                outputs[worker].pinning_applied = cpu::apply_current_thread_cpu_set(affinity_plan[worker]);
            }
            try {
                const auto process_segment = [&](const std::uint64_t segment) {
                    const auto begin = segment * options.segment_candidates;
                    const auto end = std::min(candidate_count, begin + options.segment_candidates);
                    scan_segment(
                        table, rules, columns, options, premarked, begin, end, outputs[worker]);
                };
                if (options.scheduling == Scheduling::dynamic_segments) {
                    for (;;) {
                        const auto segment = next_segment.fetch_add(1U);
                        if (segment >= segment_count) break;
                        process_segment(segment);
                    }
                } else {
                    for (std::uint64_t segment = worker; segment < segment_count;
                         segment += thread_count) {
                        process_segment(segment);
                    }
                }
            } catch (...) {
                failures[worker] = std::current_exception();
            }
            if (outputs[worker].pinning_applied) {
                cpu::clear_current_thread_cpu_set();
            }
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& failure : failures) {
        if (failure) std::rethrow_exception(failure);
    }

    Result result;
    result.candidate_count = candidate_count;
    result.eliminated_words = std::move(premarked);
    result.crt_applied = crt_applied;
    result.affinity_workers_requested =
        options.thread_placement == ThreadPlacement::scheduler_managed ? 0U : thread_count;
    result.huge_pages_applied = options.request_huge_pages && probe_huge_pages();
    result.rule_checks = premark_counters.rule_checks;
    result.modular_checks = premark_counters.modular_checks;
    result.exact_checks = premark_counters.exact_checks;
    const auto capabilities = collect_system_info().cpu;
    const bool vector_supported =
        (options.vector_mode == VectorMode::avx2 && capabilities.avx2) ||
        (options.vector_mode == VectorMode::avx512 && capabilities.avx512f);
    for (auto& output : outputs) {
        if (options.storage == CandidateStorage::list) {
            for (const auto index : output.list) {
                result.eliminated_words[static_cast<std::size_t>(index / 64U)] |=
                    std::uint64_t{1} << (index % 64U);
            }
        } else if (options.vector_mode == VectorMode::avx512 && capabilities.avx512f) {
            detail::merge_words_avx512(
                result.eliminated_words.data(), output.words.data(), word_count);
        } else if (options.vector_mode == VectorMode::avx2 && capabilities.avx2) {
            detail::merge_words_avx2(
                result.eliminated_words.data(), output.words.data(), word_count);
        } else {
            detail::merge_words_scalar(
                result.eliminated_words.data(), output.words.data(), word_count);
        }
        result.rule_checks += output.counters.rule_checks;
        result.modular_checks += output.counters.modular_checks;
        result.exact_checks += output.counters.exact_checks;
        result.thread_pinning_applied =
            result.thread_pinning_applied || output.pinning_applied;
        if (output.pinning_applied) {
            ++result.affinity_workers_applied;
        }
    }
    result.vector_mode_applied =
        options.storage == CandidateStorage::dense_bitset && vector_supported;
    for (const auto word : result.eliminated_words) {
        result.eliminated_count += static_cast<std::uint64_t>(std::popcount(word));
    }
    return result;
}

std::vector<std::uint64_t> reference_eliminated_words(
    const congruence::AffineExponentialFamily& family,
    const std::vector<std::uint64_t>& primes) {
    const auto count = checked_candidate_count(family);
    std::vector<std::uint64_t> result(static_cast<std::size_t>((count + 63U) / 64U), 0U);
    for (const auto& elimination : congruence::scalar_reference_eliminations(family, primes)) {
        const auto index = canonical_index(
            elimination.candidate.k_index, elimination.candidate.n_index, family.n.size());
        result[static_cast<std::size_t>(index / 64U)] |= std::uint64_t{1} << (index % 64U);
    }
    return result;
}

std::string result_sha256(const Result& result, const Sha256Provider& sha256) {
    std::vector<std::byte> bytes;
    bytes.reserve(result.eliminated_words.size() * 8U + 8U);
    const auto append_u64 = [&](const std::uint64_t value) {
        for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    };
    append_u64(result.candidate_count);
    for (const auto word : result.eliminated_words) append_u64(word);
    return sha256_to_hex(sha256.digest(bytes));
}

std::string describe(const Options& options) {
    std::ostringstream stream;
    stream << "storage=" << storage_text(options.storage)
           << ";orientation=" << (options.orientation == BitsetOrientation::by_k ? "k" : "n")
           << ";loop=" << (options.loop_order == LoopOrder::prime_major ? "q-major" : "candidate-major")
           << ";layout=" << (options.metadata_layout == MetadataLayout::array_of_structures ? "aos" : "soa")
           << ";segment=" << options.segment_candidates
           << ";schedule=" << (options.scheduling == Scheduling::static_partition ? "static" : "dynamic")
           << ";compressed=" << (options.compressed_classes ? "yes" : "no")
           << ";wheel-primes=" << options.wheel_prime_count
           << ";crt-primes=" << options.crt_prime_count
           << ";vector=";
    if (options.vector_mode == VectorMode::avx2) stream << "avx2";
    else if (options.vector_mode == VectorMode::avx512) stream << "avx512";
    else stream << "scalar";
    stream << ";prefetch=" << (options.explicit_prefetch ? "yes" : "no")
           << ";huge-pages=" << (options.request_huge_pages ? "requested" : "no")
           << ";placement=";
    if (options.thread_placement == ThreadPlacement::physical_core_spread) {
        stream << "physical-core-spread";
    } else if (options.thread_placement == ThreadPlacement::logical_processor_spread) {
        stream << "logical-processor-spread";
    } else {
        stream << "scheduler";
    }
    stream
           << ";threads=" << options.threads;
    return stream.str();
}

}  // namespace primeforge::family_sieve
