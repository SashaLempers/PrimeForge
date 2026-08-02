// SPDX-License-Identifier: Apache-2.0

#include "primeforge/family/family.hpp"

#include "primeforge/math/mul128.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace primeforge::family {
namespace {

enum class TokenKind {
    identifier,
    integer,
    symbol,
    end
};

struct Token {
    TokenKind kind{TokenKind::end};
    std::string text;
    std::size_t line{1U};
    std::size_t column{1U};
};

class Lexer {
public:
    explicit Lexer(const std::string_view source) : source_(source) {}

    [[nodiscard]] std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        for (;;) {
            skip_space_and_comments();
            if (position_ == source_.size()) {
                tokens.push_back({TokenKind::end, "", line_, column_});
                return tokens;
            }
            const auto token_line = line_;
            const auto token_column = column_;
            const char first = source_[position_];
            if ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_') {
                std::string text;
                while (position_ < source_.size()) {
                    const char character = source_[position_];
                    if (!((character >= 'A' && character <= 'Z') ||
                          (character >= 'a' && character <= 'z') ||
                          (character >= '0' && character <= '9') || character == '_' ||
                          character == '.')) {
                        break;
                    }
                    text.push_back(character);
                    advance();
                }
                tokens.push_back({TokenKind::identifier, std::move(text), token_line, token_column});
                continue;
            }
            if (first >= '0' && first <= '9') {
                std::string text;
                while (position_ < source_.size() &&
                       source_[position_] >= '0' && source_[position_] <= '9') {
                    text.push_back(source_[position_]);
                    advance();
                }
                tokens.push_back({TokenKind::integer, std::move(text), token_line, token_column});
                continue;
            }
            std::string symbol(1U, first);
            advance();
            if (position_ < source_.size() &&
                ((first == '=' && source_[position_] == '=') ||
                 (first == '!' && source_[position_] == '=') ||
                 (first == '<' && source_[position_] == '=') ||
                 (first == '>' && source_[position_] == '='))) {
                symbol.push_back(source_[position_]);
                advance();
            }
            if (std::string_view{";[],()+-*^<>!="}.find(first) == std::string_view::npos) {
                throw FamilyError(
                    "LEX_INVALID_CHARACTER", token_line, token_column,
                    "unsupported character in family source");
            }
            tokens.push_back({TokenKind::symbol, std::move(symbol), token_line, token_column});
        }
    }

private:
    std::string_view source_;
    std::size_t position_{};
    std::size_t line_{1U};
    std::size_t column_{1U};

    void advance() noexcept {
        if (source_[position_] == '\n') {
            ++line_;
            column_ = 1U;
        } else {
            ++column_;
        }
        ++position_;
    }

    void skip_space_and_comments() noexcept {
        while (position_ < source_.size()) {
            const char character = source_[position_];
            if (character == '#') {
                while (position_ < source_.size() && source_[position_] != '\n') {
                    advance();
                }
            } else if (character == ' ' || character == '\t' ||
                       character == '\r' || character == '\n') {
                advance();
            } else {
                break;
            }
        }
    }
};

[[nodiscard]] ExpressionPtr make_constant(const math::BigInteger& value) {
    auto expression = std::make_shared<Expression>();
    expression->kind = ExpressionKind::constant;
    expression->constant = value;
    return expression;
}

[[nodiscard]] ExpressionPtr make_parameter(std::string name) {
    auto expression = std::make_shared<Expression>();
    expression->kind = ExpressionKind::parameter;
    expression->parameter = std::move(name);
    return expression;
}

[[nodiscard]] ExpressionPtr make_unary(const ExpressionKind kind, ExpressionPtr operand) {
    auto expression = std::make_shared<Expression>();
    expression->kind = kind;
    expression->left = std::move(operand);
    return expression;
}

[[nodiscard]] ExpressionPtr make_binary(
    const ExpressionKind kind, ExpressionPtr left, ExpressionPtr right) {
    auto expression = std::make_shared<Expression>();
    expression->kind = kind;
    expression->left = std::move(left);
    expression->right = std::move(right);
    return expression;
}

[[nodiscard]] std::uint64_t estimate_definition_bits(const FamilyDefinition& definition);
[[nodiscard]] std::uint64_t estimate_exact_constraint_bits(
    const FamilyDefinition& definition);

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    [[nodiscard]] FamilyDefinition parse() {
        FamilyDefinition definition;
        bool has_family = false;
        bool has_value = false;
        bool has_objective = false;
        bool has_proof = false;
        std::set<std::string> parameter_names;
        while (current().kind != TokenKind::end) {
            const auto keyword = expect_identifier("PARSE_EXPECTED_STATEMENT");
            if (keyword.text == "family") {
                reject_duplicate(has_family, keyword, "DUPLICATE_FAMILY");
                definition.family_id = expect_identifier("PARSE_EXPECTED_FAMILY_ID").text;
                has_family = true;
            } else if (keyword.text == "param") {
                const auto name = expect_identifier("PARSE_EXPECTED_PARAMETER");
                if (!parameter_names.insert(name.text).second) {
                    fail("DUPLICATE_PARAMETER", name, "duplicate parameter " + name.text);
                }
                expect_text("in", "PARSE_EXPECTED_IN");
                expect_text("[", "PARSE_EXPECTED_BOUND_OPEN");
                const auto minimum = parse_signed_integer();
                expect_text(",", "PARSE_EXPECTED_COMMA");
                const auto maximum = parse_signed_integer();
                expect_text("]", "PARSE_EXPECTED_BOUND_CLOSE");
                if (minimum > maximum) {
                    fail("DOMAIN_REVERSED", name, "parameter minimum exceeds maximum");
                }
                definition.parameters.push_back({name.text, minimum, maximum});
            } else if (keyword.text == "allow_product") {
                definition.allowed_product_parameters.push_back(
                    expect_identifier("PARSE_EXPECTED_PARAMETER").text);
            } else if (keyword.text == "value") {
                reject_duplicate(has_value, keyword, "DUPLICATE_VALUE");
                definition.value = parse_expression();
                has_value = true;
            } else if (keyword.text == "constraint") {
                definition.constraints.push_back(parse_constraint());
            } else if (keyword.text == "objective") {
                reject_duplicate(has_objective, keyword, "DUPLICATE_OBJECTIVE");
                definition.objective = expect_identifier("PARSE_EXPECTED_OBJECTIVE").text;
                has_objective = true;
            } else if (keyword.text == "proof") {
                reject_duplicate(has_proof, keyword, "DUPLICATE_PROOF_POLICY");
                definition.proof_policy = expect_identifier("PARSE_EXPECTED_PROOF_POLICY").text;
                has_proof = true;
            } else {
                fail("PARSE_UNKNOWN_STATEMENT", keyword, "unknown statement " + keyword.text);
            }
            expect_text(";", "PARSE_EXPECTED_SEMICOLON");
        }
        if (!has_family || !has_value || !has_objective || !has_proof) {
            fail(
                "MISSING_REQUIRED_STATEMENT", current(),
                "family, value, objective, and proof statements are required");
        }
        validate(definition);
        definition.maximum_bits_estimate = estimate_definition_bits(definition);
        definition.maximum_exact_constraint_bits_estimate =
            estimate_exact_constraint_bits(definition);
        return definition;
    }

private:
    std::vector<Token> tokens_;
    std::size_t position_{};

    [[nodiscard]] const Token& current() const noexcept { return tokens_[position_]; }

    [[noreturn]] static void fail(
        const std::string& code, const Token& token, const std::string& message) {
        throw FamilyError(code, token.line, token.column, message);
    }

    static void reject_duplicate(const bool present, const Token& token, const std::string& code) {
        if (present) {
            fail(code, token, "statement may occur only once");
        }
    }

    [[nodiscard]] Token expect_identifier(const std::string& code) {
        if (current().kind != TokenKind::identifier) {
            fail(code, current(), "expected identifier");
        }
        return tokens_[position_++];
    }

    void expect_text(const std::string_view text, const std::string& code) {
        if (current().text != text) {
            fail(code, current(), "expected '" + std::string{text} + "'");
        }
        ++position_;
    }

    [[nodiscard]] std::int64_t parse_signed_integer() {
        std::string text;
        if (current().text == "-") {
            text.push_back('-');
            ++position_;
        }
        if (current().kind != TokenKind::integer) {
            fail("PARSE_EXPECTED_INTEGER", current(), "expected bounded integer");
        }
        text += current().text;
        const auto token = current();
        ++position_;
        std::int64_t value{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
            fail("INTEGER_OUT_OF_RANGE", token, "integer does not fit signed 64-bit domain");
        }
        return value;
    }

    [[nodiscard]] std::uint64_t parse_unsigned_integer() {
        if (current().kind != TokenKind::integer) {
            fail("PARSE_EXPECTED_UNSIGNED_INTEGER", current(), "expected unsigned integer");
        }
        const auto token = current();
        ++position_;
        std::uint64_t value{};
        const auto parsed = std::from_chars(
            token.text.data(), token.text.data() + token.text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != token.text.data() + token.text.size()) {
            fail("INTEGER_OUT_OF_RANGE", token, "integer does not fit unsigned 64-bit domain");
        }
        return value;
    }

    [[nodiscard]] ExpressionPtr parse_expression() { return parse_addition(); }

    [[nodiscard]] ExpressionPtr parse_addition() {
        auto left = parse_multiplication();
        while (current().text == "+" || current().text == "-") {
            const auto operation = current().text;
            ++position_;
            auto right = parse_multiplication();
            left = make_binary(
                operation == "+" ? ExpressionKind::add : ExpressionKind::subtract,
                std::move(left), std::move(right));
        }
        return left;
    }

    [[nodiscard]] ExpressionPtr parse_multiplication() {
        auto left = parse_unary();
        while (current().text == "*") {
            ++position_;
            left = make_binary(ExpressionKind::multiply, std::move(left), parse_unary());
        }
        return left;
    }

    [[nodiscard]] ExpressionPtr parse_unary() {
        if (current().text == "-") {
            ++position_;
            return make_unary(ExpressionKind::negate, parse_unary());
        }
        return parse_power();
    }

    [[nodiscard]] ExpressionPtr parse_power() {
        auto base = parse_primary();
        if (current().text == "^") {
            ++position_;
            return make_binary(ExpressionKind::power, std::move(base), parse_unary());
        }
        return base;
    }

    [[nodiscard]] ExpressionPtr parse_primary() {
        if (current().kind == TokenKind::integer) {
            math::BigInteger value;
            try {
                value = math::BigInteger::from_decimal(current().text);
            } catch (const std::invalid_argument&) {
                fail(
                    "INTEGER_LITERAL_NONCANONICAL", current(),
                    "integer literal must be canonical unsigned decimal digits");
            }
            ++position_;
            return make_constant(value);
        }
        if (current().kind == TokenKind::identifier) {
            auto name = current().text;
            ++position_;
            return make_parameter(std::move(name));
        }
        if (current().text == "(") {
            ++position_;
            auto result = parse_expression();
            expect_text(")", "PARSE_EXPECTED_PAREN_CLOSE");
            return result;
        }
        fail("PARSE_EXPECTED_EXPRESSION", current(), "expected mathematical expression");
    }

    [[nodiscard]] ComparisonOperator parse_comparison_operator() {
        const auto token = current();
        ++position_;
        if (token.text == "==") return ComparisonOperator::equal;
        if (token.text == "!=") return ComparisonOperator::not_equal;
        if (token.text == "<") return ComparisonOperator::less;
        if (token.text == "<=") return ComparisonOperator::less_equal;
        if (token.text == ">") return ComparisonOperator::greater;
        if (token.text == ">=") return ComparisonOperator::greater_equal;
        fail("PARSE_EXPECTED_COMPARISON", token, "expected comparison operator");
    }

    [[nodiscard]] Constraint parse_constraint() {
        const auto function = expect_identifier("PARSE_EXPECTED_CONSTRAINT");
        expect_text("(", "PARSE_EXPECTED_PAREN_OPEN");
        Constraint result;
        if (function.text == "gcd") {
            result.kind = ConstraintKind::gcd_equals_one;
            result.left = parse_expression();
            expect_text(",", "PARSE_EXPECTED_COMMA");
            result.right = parse_expression();
            expect_text(")", "PARSE_EXPECTED_PAREN_CLOSE");
            expect_text("==", "PARSE_EXPECTED_EQUAL");
            if (parse_unsigned_integer() != 1U) {
                fail("GCD_TARGET_UNSUPPORTED", function, "stage-8 gcd constraint must equal one");
            }
        } else if (function.text == "even" || function.text == "odd") {
            result.kind = function.text == "even" ? ConstraintKind::even : ConstraintKind::odd;
            result.left = parse_expression();
            expect_text(")", "PARSE_EXPECTED_PAREN_CLOSE");
        } else if (function.text == "compare") {
            result.kind = ConstraintKind::comparison;
            result.left = parse_expression();
            expect_text(",", "PARSE_EXPECTED_COMMA");
            result.comparison = parse_comparison_operator();
            expect_text(",", "PARSE_EXPECTED_COMMA");
            result.right = parse_expression();
            expect_text(")", "PARSE_EXPECTED_PAREN_CLOSE");
        } else if (function.text == "congruent") {
            result.kind = ConstraintKind::congruence;
            result.left = parse_expression();
            expect_text(",", "PARSE_EXPECTED_COMMA");
            result.modulus = parse_unsigned_integer();
            expect_text(",", "PARSE_EXPECTED_COMMA");
            result.residue = parse_unsigned_integer();
            expect_text(")", "PARSE_EXPECTED_PAREN_CLOSE");
            if (result.modulus < 2U || result.residue >= result.modulus) {
                fail("CONGRUENCE_DOMAIN", function, "congruence requires modulus >= 2 and residue < modulus");
            }
        } else {
            fail("CONSTRAINT_UNKNOWN", function, "unknown constraint function " + function.text);
        }
        return result;
    }

    static void validate(const FamilyDefinition& definition);
};

[[nodiscard]] std::map<std::string, Parameter> parameter_map(const FamilyDefinition& definition) {
    std::map<std::string, Parameter> result;
    for (const auto& parameter : definition.parameters) {
        result.emplace(parameter.name, parameter);
    }
    return result;
}

[[nodiscard]] bool is_constant(const ExpressionPtr& expression) noexcept {
    return expression->kind == ExpressionKind::constant;
}

[[nodiscard]] bool is_allowed_parameter(
    const ExpressionPtr& expression, const std::set<std::string>& allowed) {
    return expression->kind == ExpressionKind::parameter && allowed.contains(expression->parameter);
}

void validate_expression(
    const ExpressionPtr& expression,
    const std::map<std::string, Parameter>& parameters,
    const std::set<std::string>& allowed_products) {
    if (!expression) {
        throw FamilyError("AST_NULL", 1U, 1U, "null expression node");
    }
    switch (expression->kind) {
        case ExpressionKind::constant:
            return;
        case ExpressionKind::parameter:
            if (!parameters.contains(expression->parameter)) {
                throw FamilyError(
                    "UNKNOWN_PARAMETER", 1U, 1U,
                    "unknown parameter " + expression->parameter);
            }
            return;
        case ExpressionKind::negate:
            validate_expression(expression->left, parameters, allowed_products);
            return;
        case ExpressionKind::add:
        case ExpressionKind::subtract:
            validate_expression(expression->left, parameters, allowed_products);
            validate_expression(expression->right, parameters, allowed_products);
            return;
        case ExpressionKind::multiply:
            validate_expression(expression->left, parameters, allowed_products);
            validate_expression(expression->right, parameters, allowed_products);
            if (!is_constant(expression->left) && !is_constant(expression->right) &&
                !is_allowed_parameter(expression->left, allowed_products) &&
                !is_allowed_parameter(expression->right, allowed_products)) {
                throw FamilyError(
                    "MULTIPLICATION_NOT_AUTHORIZED", 1U, 1U,
                    "nonconstant multiplication requires allow_product for a direct parameter factor");
            }
            return;
        case ExpressionKind::power:
            validate_expression(expression->left, parameters, allowed_products);
            validate_expression(expression->right, parameters, allowed_products);
            if (!is_constant(expression->left)) {
                throw FamilyError(
                    "POWER_BASE_NOT_CONSTANT", 1U, 1U,
                    "stage-8 power base must be an integer constant");
            }
            if (expression->right->kind == ExpressionKind::constant) {
                if (expression->right->constant.is_negative() ||
                    !expression->right->constant.to_uint64_absolute().has_value()) {
                    throw FamilyError("POWER_EXPONENT_DOMAIN", 1U, 1U, "power exponent is invalid");
                }
            } else if (expression->right->kind == ExpressionKind::parameter) {
                const auto parameter = parameters.at(expression->right->parameter);
                if (parameter.minimum < 0) {
                    throw FamilyError(
                        "POWER_EXPONENT_DOMAIN", 1U, 1U,
                        "power exponent parameter must have a nonnegative domain");
                }
            } else {
                throw FamilyError(
                    "POWER_EXPONENT_NOT_CONTROLLED", 1U, 1U,
                    "power exponent must be a constant or bounded parameter");
            }
            return;
    }
}

void Parser::validate(const FamilyDefinition& definition) {
    if (definition.parameters.empty()) {
        throw FamilyError("NO_PARAMETERS", 1U, 1U, "at least one bounded parameter is required");
    }
    if (definition.objective != "primality") {
        throw FamilyError("OBJECTIVE_UNSUPPORTED", 1U, 1U, "objective must be primality");
    }
    if (definition.proof_policy != "none" &&
        definition.proof_policy != "prove_if_survives" &&
        definition.proof_policy != "required") {
        throw FamilyError("PROOF_POLICY_UNSUPPORTED", 1U, 1U, "unsupported proof policy");
    }
    const auto parameters = parameter_map(definition);
    std::set<std::string> allowed;
    for (const auto& name : definition.allowed_product_parameters) {
        if (!parameters.contains(name)) {
            throw FamilyError("UNKNOWN_ALLOWED_PRODUCT", 1U, 1U, "allow_product names unknown parameter");
        }
        allowed.insert(name);
    }
    validate_expression(definition.value, parameters, allowed);
    for (const auto& constraint : definition.constraints) {
        validate_expression(constraint.left, parameters, allowed);
        if (constraint.right) {
            validate_expression(constraint.right, parameters, allowed);
        }
    }
}

[[nodiscard]] std::string canonical_expression(const ExpressionPtr& expression) {
    switch (expression->kind) {
        case ExpressionKind::constant:
            return "C(" + expression->constant.to_decimal() + ")";
        case ExpressionKind::parameter:
            return "P(" + expression->parameter + ")";
        case ExpressionKind::negate:
            return "N(" + canonical_expression(expression->left) + ")";
        case ExpressionKind::subtract: {
            std::array<std::string, 2> terms{
                canonical_expression(expression->left),
                "N(" + canonical_expression(expression->right) + ")"};
            std::sort(terms.begin(), terms.end());
            return "A(" + terms[0] + "," + terms[1] + ")";
        }
        case ExpressionKind::add: {
            std::array<std::string, 2> terms{
                canonical_expression(expression->left), canonical_expression(expression->right)};
            std::sort(terms.begin(), terms.end());
            return "A(" + terms[0] + "," + terms[1] + ")";
        }
        case ExpressionKind::multiply: {
            std::array<std::string, 2> factors{
                canonical_expression(expression->left), canonical_expression(expression->right)};
            std::sort(factors.begin(), factors.end());
            return "M(" + factors[0] + "," + factors[1] + ")";
        }
        case ExpressionKind::power:
            return "W(" + canonical_expression(expression->left) + "," +
                   canonical_expression(expression->right) + ")";
    }
    throw std::logic_error("unknown expression kind");
}

[[nodiscard]] std::string comparison_text(const ComparisonOperator operation) {
    switch (operation) {
        case ComparisonOperator::equal: return "==";
        case ComparisonOperator::not_equal: return "!=";
        case ComparisonOperator::less: return "<";
        case ComparisonOperator::less_equal: return "<=";
        case ComparisonOperator::greater: return ">";
        case ComparisonOperator::greater_equal: return ">=";
    }
    throw std::logic_error("unknown comparison operator");
}

[[nodiscard]] std::string canonical_constraint(const Constraint& constraint) {
    switch (constraint.kind) {
        case ConstraintKind::gcd_equals_one:
        {
            std::array<std::string, 2> operands{
                canonical_expression(constraint.left), canonical_expression(constraint.right)};
            std::sort(operands.begin(), operands.end());
            return "GCD1(" + operands[0] + "," + operands[1] + ")";
        }
        case ConstraintKind::even:
            return "EVEN(" + canonical_expression(constraint.left) + ")";
        case ConstraintKind::odd:
            return "ODD(" + canonical_expression(constraint.left) + ")";
        case ConstraintKind::comparison:
            return "CMP(" + comparison_text(constraint.comparison) + "," +
                   canonical_expression(constraint.left) + "," +
                   canonical_expression(constraint.right) + ")";
        case ConstraintKind::congruence:
            return "CONG(" + canonical_expression(constraint.left) + "," +
                   std::to_string(constraint.modulus) + "," +
                   std::to_string(constraint.residue) + ")";
    }
    throw std::logic_error("unknown constraint kind");
}

[[nodiscard]] std::string quote_json(const std::string_view value) {
    std::string result{"\""};
    for (const char character : value) {
        if (character == '"') result += "\\\"";
        else if (character == '\\') result += "\\\\";
        else if (static_cast<unsigned char>(character) < 0x20U ||
                 static_cast<unsigned char>(character) >= 0x80U) {
            throw FamilyError("CANONICAL_STRING_DOMAIN", 1U, 1U, "canonical string must be printable ASCII");
        } else result.push_back(character);
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] std::uint64_t saturating_add(
    const std::uint64_t left, const std::uint64_t right) noexcept {
    return left > std::numeric_limits<std::uint64_t>::max() - right
               ? std::numeric_limits<std::uint64_t>::max()
               : left + right;
}

[[nodiscard]] std::uint64_t saturating_multiply(
    const std::uint64_t left, const std::uint64_t right) noexcept {
    return right != 0U && left > std::numeric_limits<std::uint64_t>::max() / right
               ? std::numeric_limits<std::uint64_t>::max()
               : left * right;
}

[[nodiscard]] std::uint64_t magnitude(const std::int64_t value) noexcept {
    return value < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                     : static_cast<std::uint64_t>(value);
}

[[nodiscard]] std::uint64_t integer_bit_count(const std::uint64_t value) noexcept {
    return value == 0U ? 1U : static_cast<std::uint64_t>(std::bit_width(value));
}

[[nodiscard]] std::uint64_t exponent_upper(
    const ExpressionPtr& expression, const std::map<std::string, Parameter>& parameters) {
    if (expression->kind == ExpressionKind::constant) {
        return *expression->constant.to_uint64_absolute();
    }
    return static_cast<std::uint64_t>(parameters.at(expression->parameter).maximum);
}

[[nodiscard]] std::uint64_t estimate_expression_bits(
    const ExpressionPtr& expression, const std::map<std::string, Parameter>& parameters) {
    switch (expression->kind) {
        case ExpressionKind::constant: {
            const auto absolute = expression->constant.to_uint64_absolute();
            if (absolute.has_value()) return integer_bit_count(*absolute);
            const auto decimal = expression->constant.to_decimal();
            const auto digits = static_cast<std::uint64_t>(
                decimal.size() - (expression->constant.is_negative() ? 1U : 0U));
            return saturating_add(saturating_multiply(digits, 3'322U) / 1'000U, 1U);
        }
        case ExpressionKind::parameter: {
            const auto parameter = parameters.at(expression->parameter);
            return integer_bit_count(
                std::max(magnitude(parameter.minimum), magnitude(parameter.maximum)));
        }
        case ExpressionKind::negate:
            return estimate_expression_bits(expression->left, parameters);
        case ExpressionKind::add:
        case ExpressionKind::subtract:
            return saturating_add(
                std::max(
                    estimate_expression_bits(expression->left, parameters),
                    estimate_expression_bits(expression->right, parameters)),
                1U);
        case ExpressionKind::multiply:
            return saturating_add(
                estimate_expression_bits(expression->left, parameters),
                estimate_expression_bits(expression->right, parameters));
        case ExpressionKind::power: {
            const auto exponent = exponent_upper(expression->right, parameters);
            if (exponent == 0U) return 1U;
            const auto absolute_base = expression->left->constant.to_uint64_absolute();
            if (absolute_base.has_value()) {
                if (*absolute_base <= 1U) return 1U;
                const auto ceiling_log2 = static_cast<std::uint64_t>(
                    std::bit_width(*absolute_base - 1U));
                return saturating_add(saturating_multiply(ceiling_log2, exponent), 1U);
            }
            return saturating_multiply(
                estimate_expression_bits(expression->left, parameters), exponent);
        }
    }
    return std::numeric_limits<std::uint64_t>::max();
}

[[nodiscard]] std::uint64_t estimate_definition_bits(const FamilyDefinition& definition) {
    return estimate_expression_bits(definition.value, parameter_map(definition));
}

[[nodiscard]] std::uint64_t estimate_exact_constraint_bits(
    const FamilyDefinition& definition) {
    const auto parameters = parameter_map(definition);
    std::uint64_t result = 0U;
    for (const auto& constraint : definition.constraints) {
        if (constraint.kind != ConstraintKind::comparison &&
            constraint.kind != ConstraintKind::gcd_equals_one) {
            continue;
        }
        result = std::max(result, estimate_expression_bits(constraint.left, parameters));
        if (constraint.right) {
            result = std::max(result, estimate_expression_bits(constraint.right, parameters));
        }
    }
    return result;
}

void validate_assignment(const FamilyDefinition& definition, const Assignment& assignment) {
    if (assignment.size() != definition.parameters.size()) {
        throw FamilyError("ASSIGNMENT_PARAMETER_SET", 1U, 1U, "assignment parameter set is incomplete or has extras");
    }
    for (const auto& parameter : definition.parameters) {
        const auto found = assignment.find(parameter.name);
        if (found == assignment.end()) {
            throw FamilyError("ASSIGNMENT_MISSING", 1U, 1U, "missing parameter " + parameter.name);
        }
        if (found->second < parameter.minimum || found->second > parameter.maximum) {
            throw FamilyError("ASSIGNMENT_OUT_OF_BOUNDS", 1U, 1U, "parameter outside declared bounds");
        }
    }
}

[[nodiscard]] std::uint64_t expression_exponent(
    const ExpressionPtr& expression, const Assignment& assignment) {
    if (expression->kind == ExpressionKind::constant) {
        return *expression->constant.to_uint64_absolute();
    }
    return static_cast<std::uint64_t>(assignment.at(expression->parameter));
}

[[nodiscard]] math::BigInteger evaluate_expression_exact(
    const ExpressionPtr& expression, const Assignment& assignment) {
    switch (expression->kind) {
        case ExpressionKind::constant: return expression->constant;
        case ExpressionKind::parameter: return math::BigInteger{assignment.at(expression->parameter)};
        case ExpressionKind::add:
            return evaluate_expression_exact(expression->left, assignment) +
                   evaluate_expression_exact(expression->right, assignment);
        case ExpressionKind::subtract:
            return evaluate_expression_exact(expression->left, assignment) -
                   evaluate_expression_exact(expression->right, assignment);
        case ExpressionKind::multiply:
            return evaluate_expression_exact(expression->left, assignment) *
                   evaluate_expression_exact(expression->right, assignment);
        case ExpressionKind::negate:
            return -evaluate_expression_exact(expression->left, assignment);
        case ExpressionKind::power:
            return math::power(
                evaluate_expression_exact(expression->left, assignment),
                expression_exponent(expression->right, assignment));
    }
    throw std::logic_error("unknown expression kind");
}

[[nodiscard]] std::uint64_t add_mod(
    const std::uint64_t left, const std::uint64_t right, const std::uint64_t modulus) noexcept {
    return left >= modulus - right ? left - (modulus - right) : left + right;
}

[[nodiscard]] std::uint64_t subtract_mod(
    const std::uint64_t left, const std::uint64_t right, const std::uint64_t modulus) noexcept {
    return left >= right ? left - right : modulus - (right - left);
}

[[nodiscard]] std::uint64_t evaluate_expression_modulo(
    const ExpressionPtr& expression, const Assignment& assignment, const std::uint64_t modulus) {
    switch (expression->kind) {
        case ExpressionKind::constant: return expression->constant.modulo(modulus);
        case ExpressionKind::parameter: return math::BigInteger{assignment.at(expression->parameter)}.modulo(modulus);
        case ExpressionKind::add:
            return add_mod(
                evaluate_expression_modulo(expression->left, assignment, modulus),
                evaluate_expression_modulo(expression->right, assignment, modulus), modulus);
        case ExpressionKind::subtract:
            return subtract_mod(
                evaluate_expression_modulo(expression->left, assignment, modulus),
                evaluate_expression_modulo(expression->right, assignment, modulus), modulus);
        case ExpressionKind::multiply:
            return math::multiply_mod(
                evaluate_expression_modulo(expression->left, assignment, modulus),
                evaluate_expression_modulo(expression->right, assignment, modulus), modulus);
        case ExpressionKind::negate: {
            const auto value = evaluate_expression_modulo(expression->left, assignment, modulus);
            return value == 0U ? 0U : modulus - value;
        }
        case ExpressionKind::power: {
            auto base = evaluate_expression_modulo(expression->left, assignment, modulus);
            auto exponent = expression_exponent(expression->right, assignment);
            std::uint64_t result = 1U % modulus;
            while (exponent != 0U) {
                if ((exponent & 1U) != 0U) result = math::multiply_mod(result, base, modulus);
                exponent >>= 1U;
                if (exponent != 0U) base = math::multiply_mod(base, base, modulus);
            }
            return result;
        }
    }
    throw std::logic_error("unknown expression kind");
}

[[nodiscard]] bool compare_values(
    const math::BigInteger& left,
    const math::BigInteger& right,
    const ComparisonOperator operation) {
    switch (operation) {
        case ComparisonOperator::equal: return left == right;
        case ComparisonOperator::not_equal: return left != right;
        case ComparisonOperator::less: return left < right;
        case ComparisonOperator::less_equal: return left <= right;
        case ComparisonOperator::greater: return left > right;
        case ComparisonOperator::greater_equal: return left >= right;
    }
    return false;
}

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string_view value) noexcept {
    return std::as_bytes(std::span{value.data(), value.size()});
}

}  // namespace

FamilyError::FamilyError(
    std::string code, const std::size_t line, const std::size_t column, std::string message)
    : std::runtime_error(
          std::move(message) + " [" + code + " at " + std::to_string(line) + ":" +
          std::to_string(column) + "]"),
      code_(std::move(code)), line_(line), column_(column) {}

const std::string& FamilyError::code() const noexcept { return code_; }
std::size_t FamilyError::line() const noexcept { return line_; }
std::size_t FamilyError::column() const noexcept { return column_; }

FamilyDefinition parse_family(const std::string_view source) {
    return Parser{Lexer{source}.tokenize()}.parse();
}

std::string canonical_family(const FamilyDefinition& definition) {
    auto parameters = definition.parameters;
    std::sort(parameters.begin(), parameters.end(), [](const Parameter& left, const Parameter& right) {
        return left.name < right.name;
    });
    auto allowed = definition.allowed_product_parameters;
    std::sort(allowed.begin(), allowed.end());
    allowed.erase(std::unique(allowed.begin(), allowed.end()), allowed.end());
    std::vector<std::string> constraints;
    constraints.reserve(definition.constraints.size());
    for (const auto& constraint : definition.constraints) {
        constraints.push_back(canonical_constraint(constraint));
    }
    std::sort(constraints.begin(), constraints.end());
    constraints.erase(std::unique(constraints.begin(), constraints.end()), constraints.end());

    std::string result{"{\"allowed_product_parameters\":["};
    for (std::size_t index = 0U; index < allowed.size(); ++index) {
        if (index != 0U) result.push_back(',');
        result += quote_json(allowed[index]);
    }
    result += "],\"constraints\":[";
    for (std::size_t index = 0U; index < constraints.size(); ++index) {
        if (index != 0U) result.push_back(',');
        result += quote_json(constraints[index]);
    }
    result += "],\"family_id\":" + quote_json(definition.family_id) +
              ",\"objective\":" + quote_json("PRIMALITY") + ",\"parameters\":[";
    for (std::size_t index = 0U; index < parameters.size(); ++index) {
        if (index != 0U) result.push_back(',');
        result += "{\"maximum\":" + quote_json(std::to_string(parameters[index].maximum)) +
                  ",\"minimum\":" + quote_json(std::to_string(parameters[index].minimum)) +
                  ",\"name\":" + quote_json(parameters[index].name) + "}";
    }
    std::string proof = definition.proof_policy;
    std::transform(proof.begin(), proof.end(), proof.begin(), [](const unsigned char character) {
        return static_cast<char>(character >= 'a' && character <= 'z' ? character - 'a' + 'A' : character);
    });
    result += "],\"proof_policy\":" + quote_json(proof) +
              ",\"value\":" + quote_json(canonical_expression(definition.value)) + "}";
    return result;
}

std::string canonical_family_sha256(
    const FamilyDefinition& definition, const Sha256Provider& provider) {
    const auto canonical = canonical_family(definition);
    return sha256_to_hex(provider.digest(bytes_of(canonical)));
}

std::uint64_t estimate_maximum_bits(const FamilyDefinition& definition) {
    return definition.maximum_bits_estimate != 0U
               ? definition.maximum_bits_estimate
               : estimate_definition_bits(definition);
}

math::BigInteger evaluate_exact(
    const FamilyDefinition& definition, const Assignment& assignment) {
    validate_assignment(definition, assignment);
    constexpr std::uint64_t maximum_exact_bits = 10'000'000U;
    if (estimate_maximum_bits(definition) > maximum_exact_bits) {
        throw FamilyError("VALUE_SIZE_LIMIT", 1U, 1U, "estimated value exceeds exact-evaluation limit");
    }
    return evaluate_expression_exact(definition.value, assignment);
}

std::uint64_t evaluate_modulo(
    const FamilyDefinition& definition,
    const Assignment& assignment,
    const std::uint64_t modulus) {
    if (modulus == 0U) {
        throw FamilyError("MODULUS_ZERO", 1U, 1U, "evaluation modulus must be nonzero");
    }
    validate_assignment(definition, assignment);
    return evaluate_expression_modulo(definition.value, assignment, modulus);
}

bool constraints_satisfied(
    const FamilyDefinition& definition, const Assignment& assignment) {
    validate_assignment(definition, assignment);
    constexpr std::uint64_t maximum_exact_bits = 10'000'000U;
    const auto exact_constraint_bits = definition.maximum_exact_constraint_bits_estimate != 0U
                                           ? definition.maximum_exact_constraint_bits_estimate
                                           : estimate_exact_constraint_bits(definition);
    if (exact_constraint_bits > maximum_exact_bits) {
        throw FamilyError(
            "CONSTRAINT_SIZE_LIMIT", 1U, 1U,
            "estimated exact constraint operand exceeds evaluation limit");
    }
    for (const auto& constraint : definition.constraints) {
        switch (constraint.kind) {
            case ConstraintKind::even:
                if (evaluate_expression_modulo(constraint.left, assignment, 2U) != 0U) return false;
                break;
            case ConstraintKind::odd:
                if (evaluate_expression_modulo(constraint.left, assignment, 2U) != 1U) return false;
                break;
            case ConstraintKind::congruence:
                if (evaluate_expression_modulo(
                        constraint.left, assignment, constraint.modulus) != constraint.residue) return false;
                break;
            case ConstraintKind::comparison:
                if (!compare_values(
                        evaluate_expression_exact(constraint.left, assignment),
                        evaluate_expression_exact(constraint.right, assignment),
                        constraint.comparison)) return false;
                break;
            case ConstraintKind::gcd_equals_one: {
                const auto left = evaluate_expression_exact(constraint.left, assignment).to_uint64_absolute();
                const auto right = evaluate_expression_exact(constraint.right, assignment).to_uint64_absolute();
                if (!left.has_value() || !right.has_value()) {
                    throw FamilyError(
                        "GCD_OPERAND_LIMIT", 1U, 1U,
                        "stage-8 gcd operands must fit unsigned 64-bit");
                }
                if (std::gcd(*left, *right) != 1U) return false;
                break;
            }
        }
    }
    return true;
}

}  // namespace primeforge::family
