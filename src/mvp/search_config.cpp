// SPDX-License-Identifier: Apache-2.0

#include "primeforge/mvp/search_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace primeforge::mvp {
namespace {

[[nodiscard]] std::string_view trim(const std::string_view value) noexcept {
    std::size_t begin = 0U;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\r')) ++begin;
    auto end = value.size();
    while (end > begin && (value[end - 1U] == ' ' || value[end - 1U] == '\r')) --end;
    return value.substr(begin, end - begin);
}

void require_ascii(const std::string_view value, const std::string_view field) {
    for (const unsigned char byte : value) {
        if (byte < 0x20U || byte >= 0x7fU) {
            throw std::invalid_argument(std::string{field} + " must use printable ASCII");
        }
    }
}

[[nodiscard]] std::string parse_scalar(const std::string_view source) {
    const auto value = trim(source);
    if (value.empty()) throw std::invalid_argument("YAML scalar must not be empty");
    if (value.front() != '"') {
        require_ascii(value, "YAML scalar");
        if (value.find('#') != std::string_view::npos) {
            throw std::invalid_argument("inline YAML comments are not supported");
        }
        return std::string{value};
    }
    if (value.size() < 2U || value.back() != '"') {
        throw std::invalid_argument("unterminated quoted YAML scalar");
    }
    std::string result;
    for (std::size_t index = 1U; index + 1U < value.size(); ++index) {
        const char character = value[index];
        if (character == '\\') {
            if (index + 1U >= value.size() - 1U) {
                throw std::invalid_argument("unterminated YAML escape");
            }
            const char escaped = value[++index];
            if (escaped != '\\' && escaped != '"') {
                throw std::invalid_argument("only quote and backslash YAML escapes are supported");
            }
            result.push_back(escaped);
        } else if (character == '"') {
            throw std::invalid_argument("unescaped quote in YAML scalar");
        } else {
            result.push_back(character);
        }
    }
    require_ascii(result, "YAML scalar");
    return result;
}

[[nodiscard]] std::map<std::string, std::string> parse_mapping(
    const std::string_view source) {
    std::map<std::string, std::string> result;
    std::set<std::string> declared_keys;
    std::vector<std::string> sections;
    std::size_t line_number = 0U;
    std::size_t offset = 0U;
    while (offset <= source.size()) {
        const auto newline = source.find('\n', offset);
        const auto end = newline == std::string_view::npos ? source.size() : newline;
        const auto line = source.substr(offset, end - offset);
        ++line_number;
        offset = newline == std::string_view::npos ? source.size() + 1U : newline + 1U;

        std::size_t indentation = 0U;
        while (indentation < line.size() && line[indentation] == ' ') ++indentation;
        if (indentation < line.size() && line[indentation] == '\t') {
            throw std::invalid_argument("tabs are forbidden in search YAML at line " +
                                        std::to_string(line_number));
        }
        const auto content = trim(line.substr(indentation));
        if (content.empty() || content.front() == '#') continue;
        if ((indentation % 2U) != 0U) {
            throw std::invalid_argument("YAML indentation must use two spaces at line " +
                                        std::to_string(line_number));
        }
        const auto level = indentation / 2U;
        if (level > sections.size()) {
            throw std::invalid_argument("YAML indentation skips a level at line " +
                                        std::to_string(line_number));
        }
        const auto separator = content.find(':');
        if (separator == std::string_view::npos) {
            throw std::invalid_argument("YAML mapping entry lacks colon at line " +
                                        std::to_string(line_number));
        }
        const auto key_view = trim(content.substr(0U, separator));
        if (key_view.empty() || !std::ranges::all_of(key_view, [](const char value) {
                return (value >= 'a' && value <= 'z') ||
                       (value >= '0' && value <= '9') || value == '_';
            })) {
            throw std::invalid_argument("invalid YAML key at line " +
                                        std::to_string(line_number));
        }
        sections.resize(level);
        std::string full_key;
        for (const auto& section : sections) {
            if (!full_key.empty()) full_key.push_back('.');
            full_key += section;
        }
        if (!full_key.empty()) full_key.push_back('.');
        full_key.append(key_view);
        if (!declared_keys.emplace(full_key).second) {
            throw std::invalid_argument("duplicate YAML key: " + full_key);
        }
        const auto value_view = trim(content.substr(separator + 1U));
        if (value_view.empty()) {
            static const std::set<std::string> allowed_sections{
                "checkpoint", "constraints", "engines", "engines.flint",
                "engines.pari_gp", "output", "parameters", "parameters.k",
                "parameters.n", "sieve", "work_units"};
            if (!allowed_sections.contains(full_key)) {
                throw std::invalid_argument("unknown YAML section: " + full_key);
            }
            sections.push_back(std::string{key_view});
            continue;
        }
        result.emplace(full_key, parse_scalar(value_view));
    }
    return result;
}

[[nodiscard]] const std::string& required(
    const std::map<std::string, std::string>& fields, const std::string& key) {
    const auto found = fields.find(key);
    if (found == fields.end()) throw std::invalid_argument("missing YAML key: " + key);
    return found->second;
}

[[nodiscard]] std::uint64_t parse_u64(
    const std::map<std::string, std::string>& fields, const std::string& key) {
    const auto& value = required(fields, key);
    if (value.empty() || (value.size() > 1U && value.front() == '0') ||
        !std::ranges::all_of(value, [](const char digit) {
            return digit >= '0' && digit <= '9';
        })) {
        throw std::invalid_argument(key + " must be a canonical unsigned decimal");
    }
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size()) throw std::invalid_argument(key + " is not an integer");
    return parsed;
}

[[nodiscard]] bool parse_true(
    const std::map<std::string, std::string>& fields, const std::string& key) {
    if (required(fields, key) != "true") {
        throw std::invalid_argument(key + " must be true in the MVP");
    }
    return true;
}

[[nodiscard]] std::filesystem::path parse_relative_path(
    const std::map<std::string, std::string>& fields, const std::string& key) {
    const auto& value = required(fields, key);
    require_ascii(value, key);
    if (value.empty() || value.find('\\') != std::string::npos) {
        throw std::invalid_argument(key + " must be a nonempty portable path using '/'");
    }
    const std::filesystem::path path{value};
    if (path.is_absolute() || path.has_root_name()) {
        throw std::invalid_argument(key + " must be relative");
    }
    const auto normalized = path.lexically_normal();
    for (const auto& component : normalized) {
        if (component == "..") throw std::invalid_argument(key + " must stay within the run root");
    }
    if (normalized.empty() || normalized == ".") {
        throw std::invalid_argument(key + " must name a path");
    }
    return normalized;
}

[[nodiscard]] std::string parse_sha256(
    const std::map<std::string, std::string>& fields, const std::string& key) {
    const auto& value = required(fields, key);
    const auto digest = sha256_from_hex(value);
    if (!digest.has_value() || sha256_to_hex(*digest) != value) {
        throw std::invalid_argument(key + " must be lowercase SHA-256 hex");
    }
    return value;
}

[[nodiscard]] std::uint64_t progression_count(
    const std::uint64_t start,
    const std::uint64_t stop,
    const std::uint64_t step,
    const std::string_view name) {
    if (step == 0U || start > stop || (stop - start) % step != 0U) {
        throw std::invalid_argument(std::string{name} + " progression must be nonempty and include stop");
    }
    return (stop - start) / step + 1U;
}

void validate_config(const SearchConfig& config) {
    if (config.schema != "primeforge.search.v1" || config.family != "proth" ||
        config.expression != "k*2^n+1") {
        throw std::invalid_argument("MVP supports only schema primeforge.search.v1 and k*2^n+1");
    }
    if (config.campaign_name.empty() || config.campaign_name == "." ||
        config.campaign_name == ".." ||
        !std::ranges::all_of(config.campaign_name, [](const char value) {
            return (value >= 'a' && value <= 'z') ||
                   (value >= '0' && value <= '9') || value == '-' || value == '_';
        })) {
        throw std::invalid_argument("campaign_name must be a safe lowercase identifier");
    }
    const auto k_count = progression_count(
        config.k_start, config.k_stop, config.k_step, "k");
    const auto n_count = progression_count(
        config.n_start, config.n_stop, config.n_step, "n");
    if (!config.require_odd_k || !config.require_k_less_than_power ||
        config.k_start == 0U || (config.k_start & 1U) == 0U ||
        (config.k_step & 1U) != 0U) {
        throw std::invalid_argument("MVP requires a positive odd-k progression");
    }
    if (config.n_start == 0U || config.n_stop >= 64U ||
        config.k_stop >= (std::uint64_t{1} << config.n_start)) {
        throw std::invalid_argument("every MVP candidate must satisfy positive n and k<2^n");
    }
    if (config.k_stop > (std::numeric_limits<std::uint64_t>::max() - 1U) >> config.n_stop) {
        throw std::invalid_argument("MVP candidate exceeds uint64_t");
    }
    if (k_count > std::numeric_limits<std::uint64_t>::max() / n_count) {
        throw std::invalid_argument("campaign candidate count overflows");
    }
    if (config.k_stop > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        config.k_step > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("MVP progression exceeds the congruence compiler range");
    }
    if (config.work_unit_candidates == 0U || config.checkpoint_every_candidates == 0U ||
        config.sieve_maximum_prime < 2U || config.sieve_maximum_prime > 1'000'000U) {
        throw std::invalid_argument("invalid work-unit, checkpoint, or sieve bound");
    }
    static_cast<void>(parse_relative_path(
        {{"value", config.output_directory.generic_string()}}, "value"));
    for (const auto* engine : {&config.pari_gp, &config.flint}) {
        static_cast<void>(parse_relative_path(
            {{"value", engine->path.generic_string()}}, "value"));
        const auto digest = sha256_from_hex(engine->expected_sha256);
        if (!digest.has_value() || sha256_to_hex(*digest) != engine->expected_sha256) {
            throw std::invalid_argument("engine SHA-256 must be lowercase canonical hex");
        }
    }
}

[[nodiscard]] std::string quote_json(const std::string_view value) {
    std::string result{"\""};
    for (const unsigned char byte : value) {
        if (byte == '"') result += "\\\"";
        else if (byte == '\\') result += "\\\\";
        else {
            if (byte < 0x20U || byte >= 0x7fU) {
                throw std::invalid_argument("canonical MVP JSON uses printable ASCII strings");
            }
            result.push_back(static_cast<char>(byte));
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string_view text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

}  // namespace

SearchConfig parse_search_config(const std::string_view yaml) {
    const auto fields = parse_mapping(yaml);
    static const std::set<std::string> allowed{
        "campaign_name", "checkpoint.every_candidates", "constraints.k_less_than_2_pow_n",
        "constraints.odd_k", "engines.flint.path", "engines.flint.sha256",
        "engines.pari_gp.path", "engines.pari_gp.sha256", "expression", "family",
        "output.directory", "parameters.k.start", "parameters.k.step", "parameters.k.stop",
        "parameters.n.start", "parameters.n.step", "parameters.n.stop", "schema",
        "sieve.maximum_prime", "work_units.candidates_per_unit"};
    for (const auto& [key, value] : fields) {
        static_cast<void>(value);
        if (!allowed.contains(key)) throw std::invalid_argument("unknown YAML key: " + key);
    }

    SearchConfig result;
    result.schema = required(fields, "schema");
    result.campaign_name = required(fields, "campaign_name");
    result.family = required(fields, "family");
    result.expression = required(fields, "expression");
    result.k_start = parse_u64(fields, "parameters.k.start");
    result.k_stop = parse_u64(fields, "parameters.k.stop");
    result.k_step = parse_u64(fields, "parameters.k.step");
    result.n_start = parse_u64(fields, "parameters.n.start");
    result.n_stop = parse_u64(fields, "parameters.n.stop");
    result.n_step = parse_u64(fields, "parameters.n.step");
    result.require_odd_k = parse_true(fields, "constraints.odd_k");
    result.require_k_less_than_power = parse_true(
        fields, "constraints.k_less_than_2_pow_n");
    result.work_unit_candidates = parse_u64(fields, "work_units.candidates_per_unit");
    result.sieve_maximum_prime = parse_u64(fields, "sieve.maximum_prime");
    result.checkpoint_every_candidates = parse_u64(fields, "checkpoint.every_candidates");
    result.output_directory = parse_relative_path(fields, "output.directory");
    result.pari_gp.path = parse_relative_path(fields, "engines.pari_gp.path");
    result.pari_gp.expected_sha256 = parse_sha256(fields, "engines.pari_gp.sha256");
    result.flint.path = parse_relative_path(fields, "engines.flint.path");
    result.flint.expected_sha256 = parse_sha256(fields, "engines.flint.sha256");
    validate_config(result);
    return result;
}

SearchConfig load_search_config(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("cannot open search configuration: " + path.string());
    const std::string content{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (content.starts_with("\xef\xbb\xbf")) {
        throw std::invalid_argument("search YAML must be UTF-8 without BOM");
    }
    return parse_search_config(content);
}

std::string canonical_search_config(const SearchConfig& config) {
    validate_config(config);
    const auto decimal = [](const std::uint64_t value) { return quote_json(std::to_string(value)); };
    return "{\"campaign_name\":" + quote_json(config.campaign_name) +
           ",\"checkpoint_every_candidates\":" + decimal(config.checkpoint_every_candidates) +
           ",\"constraints\":{\"k_less_than_2_pow_n\":true,\"odd_k\":true}" +
           ",\"engines\":{\"flint\":{\"path\":" +
           quote_json(config.flint.path.generic_string()) + ",\"sha256\":" +
           quote_json(config.flint.expected_sha256) + "},\"pari_gp\":{\"path\":" +
           quote_json(config.pari_gp.path.generic_string()) + ",\"sha256\":" +
           quote_json(config.pari_gp.expected_sha256) + "}}" +
           ",\"expression\":" + quote_json(config.expression) +
           ",\"family\":" + quote_json(config.family) +
           ",\"output_directory\":" + quote_json(config.output_directory.generic_string()) +
           ",\"parameters\":{\"k\":{\"start\":" + decimal(config.k_start) +
           ",\"step\":" + decimal(config.k_step) + ",\"stop\":" + decimal(config.k_stop) +
           "},\"n\":{\"start\":" + decimal(config.n_start) +
           ",\"step\":" + decimal(config.n_step) + ",\"stop\":" + decimal(config.n_stop) + "}}" +
           ",\"pipeline_version\":" + quote_json(pipeline_version) +
           ",\"proof_policy\":" + quote_json(pipeline_proof_policy) +
           ",\"schema\":" + quote_json(config.schema) +
           ",\"sieve_maximum_prime\":" + decimal(config.sieve_maximum_prime) +
           ",\"work_unit_candidates\":" + decimal(config.work_unit_candidates) + "}";
}

std::string render_search_config_yaml(const SearchConfig& config) {
    validate_config(config);
    const auto quote_yaml = [](const std::string_view value) {
        std::string result{"\""};
        for (const char character : value) {
            if (character == '\\' || character == '"') result.push_back('\\');
            result.push_back(character);
        }
        result.push_back('"');
        return result;
    };
    return "# SPDX-License-Identifier: Apache-2.0\n"
           "# Canonical PrimeForge campaign copy for recovery and verification.\n"
           "schema: " + quote_yaml(config.schema) + "\n"
           "campaign_name: " + quote_yaml(config.campaign_name) + "\n"
           "family: " + quote_yaml(config.family) + "\n"
           "expression: " + quote_yaml(config.expression) + "\n"
           "parameters:\n"
           "  k:\n"
           "    start: " + std::to_string(config.k_start) + "\n"
           "    stop: " + std::to_string(config.k_stop) + "\n"
           "    step: " + std::to_string(config.k_step) + "\n"
           "  n:\n"
           "    start: " + std::to_string(config.n_start) + "\n"
           "    stop: " + std::to_string(config.n_stop) + "\n"
           "    step: " + std::to_string(config.n_step) + "\n"
           "constraints:\n"
           "  odd_k: true\n"
           "  k_less_than_2_pow_n: true\n"
           "work_units:\n"
           "  candidates_per_unit: " + std::to_string(config.work_unit_candidates) + "\n"
           "sieve:\n"
           "  maximum_prime: " + std::to_string(config.sieve_maximum_prime) + "\n"
           "checkpoint:\n"
           "  every_candidates: " + std::to_string(config.checkpoint_every_candidates) + "\n"
           "output:\n"
           "  directory: " + quote_yaml(config.output_directory.generic_string()) + "\n"
           "engines:\n"
           "  pari_gp:\n"
           "    path: " + quote_yaml(config.pari_gp.path.generic_string()) + "\n"
           "    sha256: " + quote_yaml(config.pari_gp.expected_sha256) + "\n"
           "  flint:\n"
           "    path: " + quote_yaml(config.flint.path.generic_string()) + "\n"
           "    sha256: " + quote_yaml(config.flint.expected_sha256) + "\n";
}

std::string search_config_sha256(
    const SearchConfig& config, const Sha256Provider& sha256) {
    const auto canonical = canonical_search_config(config);
    return sha256_to_hex(sha256.digest(bytes_of(canonical)));
}

std::uint64_t candidate_count(const SearchConfig& config) {
    validate_config(config);
    return progression_count(config.k_start, config.k_stop, config.k_step, "k") *
           progression_count(config.n_start, config.n_stop, config.n_step, "n");
}

CandidateCoordinates candidate_at(
    const SearchConfig& config, const std::uint64_t flat_index) {
    const auto count = candidate_count(config);
    if (flat_index >= count) throw std::out_of_range("candidate index outside campaign");
    const auto n_count = progression_count(config.n_start, config.n_stop, config.n_step, "n");
    const auto k_index = flat_index / n_count;
    const auto n_index = flat_index % n_count;
    const auto k = config.k_start + k_index * config.k_step;
    const auto n = config.n_start + n_index * config.n_step;
    return {flat_index, k, n, (k << n) + 1U};
}

congruence::AffineExponentialFamily make_affine_family(const SearchConfig& config) {
    validate_config(config);
    congruence::AffineExponentialFamily result;
    result.k = {
        static_cast<std::int64_t>(config.k_start),
        static_cast<std::int64_t>(config.k_stop),
        config.k_step};
    result.n = {
        static_cast<std::int64_t>(config.n_start),
        static_cast<std::int64_t>(config.n_stop),
        config.n_step};
    result.base = 2;
    result.constant = 1;
    result.k_parity = congruence::ParityConstraint::odd;
    return result;
}

CampaignPlan build_campaign_plan(
    const SearchConfig& config, const Sha256Provider& sha256) {
    CampaignPlan result;
    result.configuration_sha256 = search_config_sha256(config, sha256);
    result.campaign_id = "sha256:" + result.configuration_sha256;
    result.candidate_count = candidate_count(config);
    work::WorkUnit prototype;
    prototype.family_id = "primeforge.proth.uint64.v1";
    prototype.canonical_definition_sha256 = result.configuration_sha256;
    prototype.constraints = {"gcd(k,2)=1", "k<2^n", "k%2=1"};
    prototype.residue_compiler_version = "primeforge-congruence-v1";
    prototype.sieve_bounds = {2U, config.sieve_maximum_prime};
    prototype.proof_policy = pipeline_proof_policy;
    result.work_units = work::partition_work_units(
        prototype, 0U, result.candidate_count, config.work_unit_candidates, sha256);
    result.coverage = work::verify_coverage(
        0U, result.candidate_count, result.work_units, sha256);
    if (!result.coverage.valid) throw std::logic_error("generated campaign plan failed coverage");
    return result;
}

std::string canonical_inspection(const CampaignPlan& plan) {
    std::string units{"["};
    for (std::size_t index = 0U; index < plan.work_units.size(); ++index) {
        if (index != 0U) units.push_back(',');
        units += work::canonical_work_unit(plan.work_units[index]);
    }
    units.push_back(']');
    return "{\"campaign_id\":" + quote_json(plan.campaign_id) +
           ",\"candidate_count\":" + quote_json(std::to_string(plan.candidate_count)) +
           ",\"configuration_sha256\":" + quote_json(plan.configuration_sha256) +
           ",\"coverage\":" + plan.coverage.canonical_report_json +
           ",\"work_units\":" + units + "}";
}

}  // namespace primeforge::mvp
