// SPDX-License-Identifier: Apache-2.0

#pragma once


#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace primeforge::runtime::internal {

[[nodiscard]] inline std::string json_escape(const std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() + 2U);
    result.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20U) {
                result += "\\u00";
                result.push_back(hex[character >> 4U]);
                result.push_back(hex[character & 0x0fU]);
            } else {
                result.push_back(static_cast<char>(character));
            }
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] inline std::string trim(std::string value) {
    const auto whitespace = [](const unsigned char value) { return std::isspace(value) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), whitespace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), whitespace).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

[[nodiscard]] inline std::vector<std::string> split_csv(const std::string_view line) {
    std::vector<std::string> fields;
    std::size_t begin = 0U;
    while (begin <= line.size()) {
        const auto comma = line.find(',', begin);
        const auto end = comma == std::string_view::npos ? line.size() : comma;
        fields.push_back(trim(std::string(line.substr(begin, end - begin))));
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1U;
    }
    return fields;
}

[[nodiscard]] inline std::optional<double> parse_double(const std::string_view text) {
    double value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] inline std::string unescape_json_string(const std::string_view value) {
    std::string result;
    for (std::size_t index = 0U; index < value.size(); ++index) {
        if (value[index] != '\\') {
            result.push_back(value[index]);
            continue;
        }
        if (++index >= value.size()) {
            throw std::runtime_error("truncated JSON escape");
        }
        switch (value[index]) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: throw std::runtime_error("unsupported JSON escape");
        }
    }
    return result;
}

} // namespace primeforge::runtime::internal
