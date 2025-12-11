#include "absl/strings/numbers.h"
#include "hrz/common/color.h"
#include "hrz/common/profiling.h"
#include "hrz/core/style/enums.h"
#include "hrz/core/style/script.h"
#include "hrz/fnd/char_utils.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/string_utils.h"

#define CHECK_ERR(...)            \
    do                            \
    {                             \
        Result err = __VA_ARGS__; \
        if (!err) return err;     \
    } while (0)

#define EXPECT_TYPE(TYPES, T)                                \
    do                                                       \
    {                                                        \
        if (!(TYPES).can_be(T))                              \
        {                                                    \
            return Result{Result::MismatchType, token.line}; \
        }                                                    \
    } while (0)

#define EXPECT_TOKEN(TOKEN, KIND)                               \
    do                                                          \
    {                                                           \
        CHECK_ERR(lexer.next(&TOKEN));                          \
        if (TOKEN.kind != Token::KIND)                          \
        {                                                       \
            return Result{Result::UnexpectedToken, TOKEN.line}; \
        }                                                       \
    } while (0)

namespace
{
using namespace hrz;
using namespace style;

constexpr inline std::optional<Operator> comparison_token_to_operator(Token::Kind kind)
{
    switch (kind)
    {
        case Token::EqCmp: return Operator::Eq;
        case Token::NeqCmp: return Operator::Neq;
        case Token::LtCmp: return Operator::Lt;
        case Token::GtCmp: return Operator::Gt;
        case Token::LeqCmp: return Operator::Leq;
        case Token::GeqCmp: return Operator::Geq;
        default: assert(!"Unhandled case"); break;
    }
    return std::nullopt;
}

constexpr inline bool is_comparison_operator(Token::Kind kind)
{
    return kind == Token::Eq || kind == Token::EqCmp || kind == Token::NeqCmp
        || kind == Token::LtCmp || kind == Token::GtCmp || kind == Token::LeqCmp
        || kind == Token::GeqCmp;
}

struct ParserImpl : public Parser
{
#if HRZ_DEBUG
    hrz::flat_hash_set<uint64_t> _registered_property_ids;
#endif

    hrz::flat_hash_map<hrz::uint128, uint32_t> _attributes;
    hrz::flat_hash_map<hrz::uint128, uint64_t> _properties;
    hrz::flat_hash_map<hrz::uint128, uint32_t> _palettes;
    hrz::flat_hash_map<hrz::uint128, uint32_t> _uniforms;

    int add_attribute(std::string_view name) override
    {
        if (_attributes.size() < UINT32_MAX)
        {
            auto in_name = hrz::murmur3_x64_128(name);

            auto it = _attributes.find(in_name);
            if (it == _attributes.end())
            {
                int id = (int)_attributes.size();
                _attributes.insert(std::make_pair(in_name, (uint32_t)id));
                return id;
            }
            else
            {
                return (int)it->second;
            }
        }
        else
        {
            HRZ_LOG_ERROR("Too many attributes");
            return INSERT_ERROR;
        }
    }

    int add_uniform(std::string_view name) override
    {
        auto in_name = hrz::murmur3_x64_128(name);

        auto it = _uniforms.find(in_name);
        if (it == _uniforms.end())
        {
            int id = (int)_uniforms.size();
            _uniforms.insert(std::make_pair(in_name, id));
            return id;
        }
        else
        {
            HRZ_LOG_ERROR("Adding a duplicate uniform \"{}\".", name);
        }

        return INSERT_ERROR;
    }

    void add_property(uint64_t id, std::string_view name) override
    {
        auto in_name = hrz::murmur3_x64_128(name);

        auto it = _properties.find(in_name);
        if (it == _properties.end())
        {
#if HRZ_DEBUG
            if (_registered_property_ids.count(id) > 0)
            {
                HRZ_LOG_ERROR("Adding property \"{}\" with duplicate id {}.", name, id);
                return;
            }
            _registered_property_ids.insert(id);
#endif
            _properties.insert(std::make_pair(in_name, id));
        }
        else
        {
            HRZ_LOG_ERROR("Adding duplicate property \"{}\", please don't.", name);
        }
    }

    void add_palette(std::string_view name) override
    {
        if (_palettes.size() < UINT32_MAX)
        {
            auto hash = hrz::murmur3_x64_128(name);

            auto it = _palettes.find(hash);
            if (it == _palettes.end())
            {
                uint32_t palette_id = _palettes.size();
                _palettes.insert(std::make_pair(hash, palette_id));
                return;
            }
        }
        else
        {
            HRZ_LOG_ERROR("Too many palettes");
        }
    }

    Result parse_attribute(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        Token token;
        EXPECT_TOKEN(token, Attr);
        EXPECT_TOKEN(token, OpenParen);
        EXPECT_TOKEN(token, StringLiteral);

        auto it = _attributes.find(hrz::murmur3_x64_128(token.span));
        if (it == _attributes.end())
        {
            return Result{Result::UnknownAttribute, token.line};
        }
        else
        {
            Ast::Node node(NodeKind::Attribute);
            node.data.value = it->second;

            *subtree = ast.nodes.size();
            ast.nodes.push_back(node);
        }

        EXPECT_TOKEN(token, CloseParen);
        return Result{Result::Ok, 0};
    }

    Result parse_uniform(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        Token token;
        EXPECT_TOKEN(token, Uniform);
        EXPECT_TOKEN(token, OpenParen);
        EXPECT_TOKEN(token, StringLiteral);

        auto it = _uniforms.find(hrz::murmur3_x64_128(token.span));
        if (it == _uniforms.end())
        {
            return Result{Result::UnknownUniform, token.line};
        }
        else
        {
            const auto& uniform = it->second;

            Ast::Node node(NodeKind::Uniform);
            node.data.value = uniform;

            *subtree = ast.nodes.size();
            ast.nodes.push_back(node);
        }

        EXPECT_TOKEN(token, CloseParen);
        return Result{Result::Ok, 0};
    }

    Result parse_enum(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        Token token;
        EXPECT_TOKEN(token, Enum);
        EXPECT_TOKEN(token, OpenParen);

        Token enum_name_token;
        EXPECT_TOKEN(enum_name_token, StringLiteral);

        EXPECT_TOKEN(token, Comma);

        Token value_name_token;
        EXPECT_TOKEN(value_name_token, StringLiteral);

        EXPECT_TOKEN(token, CloseParen);

        auto res = find_enum_value_by_name(enum_name_token.span, value_name_token.span);
        switch (res.type)
        {
            case EnumFindResult::Type::Ok:
            {
                Ast::Node node(NodeKind::Literal);
                node.literal = hrz::vector_data::attr_from<RawValue>(res.value);

                *subtree = ast.nodes.size();
                ast.nodes.push_back(node);
                break;
            }
            case EnumFindResult::Type::UnknownEnum: return Result{Result::UnknownEnum, token.line};
            case EnumFindResult::Type::UnknownValue:
                return Result{Result::UnknownEnumValue, token.line};
            default: assert(false && "Unhandled case"); return Result{Result::Ok, 0};
        }
        return Result{Result::Ok, 0};
    }

    Result parse_property(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        Token token;
        EXPECT_TOKEN(token, Property);
        EXPECT_TOKEN(token, OpenParen);
        EXPECT_TOKEN(token, StringLiteral);

        auto it = _properties.find(hrz::murmur3_x64_128(token.span));
        if (it == _properties.end())
        {
            return Result{Result::UnknownProperty, token.line};
        }
        else
        {
            Ast::Node node(NodeKind::Property);
            node.data.value = it->second;

            *subtree = ast.nodes.size();
            ast.nodes.push_back(node);
        }

        EXPECT_TOKEN(token, CloseParen);
        return Result{Result::Ok, 0};
    }

    static RawValue make_number_literal(
        int sign,
        uint64_t integer_u64,
        double integer_f64,
        std::optional<double> decimal)
    {
        constexpr double kInt64MinAbs = -(double)INT64_MIN;
        // UINT64_MAX isn't precisely representable on a double but UINT64_MAX + 1 is. So we use
        // this for comparisons.
        constexpr double k2Pow64 = 18446744073709551616.0;

        RawValue raw_value = vector_data::attr_from<RawValue>(0.0);

        if (decimal.has_value())
        {
            raw_value = vector_data::attr_from<RawValue>(sign * (integer_f64 + decimal.value()));
        }
        else if (sign < 0.0)
        {
            if (integer_f64 > kInt64MinAbs)
            {
                // Too large to be representable as int64_t.
                raw_value = vector_data::attr_from<RawValue>(sign * integer_f64);
            }
            else
            {
                raw_value = vector_data::attr_from<RawValue>(sign * (int64_t)integer_u64);
            }
        }
        else
        {
            // @Todo: The following isn't precise because the integer part could be representable by
            // a uint64_t but not by a double. In this case, depending on the rounding mode,
            // 'integer_f64' can be rounded up so that 'integer_f64 == k2Pow64'. When this happens,
            // the branch taken whereas it shouldn't. Note that this can happen only for a small
            // number of values close to 2^64.
            if (integer_f64 >= k2Pow64)
            {
                // Too large to be representable as a uint64_t.
                raw_value = vector_data::attr_from<RawValue>(integer_f64);
            }
            else
            {
                raw_value = vector_data::attr_from<RawValue>(integer_u64);
            }
        }

        return raw_value;
    }

    Result parse_numeric_value(const Token& token, RawValue* value) const
    {
        std::string_view s = token.span;

        int sign = s[0] == '-' ? -1 : 1;
        if (s[0] == '-') s.remove_prefix(1);

        if (s.size() > 2 && s[0] == '0' && s[1] == 'x')
        {
            // Parse hexadecimal literal.
            uint64_t raw = 0;
            if (!absl::SimpleHexAtoi<uint64_t>(s, &raw))
            {
                return Result{Result::InvalidNumericLiteral, token.line};
            }
            *value = vector_data::attr_from<RawValue>(raw);
        }
        else
        {
            // Parse integer or decimal literal.

            int pos = hrz::str::find(s, '.');
            std::string_view s_int = pos < 0 ? s : s.substr(0, pos);

            double raw_f64 = 0;
            if (!absl::SimpleAtod(s, &raw_f64))
            {
                return Result{Result::InvalidNumericLiteral, token.line};
            }

            uint64_t raw_u64 = 0;
            if (!absl::SimpleAtoi<uint64_t>(s_int, &raw_u64))
            {
                // This can happen when the number is a real of the form '.1234' or when the number
                // isn't representable by a uint64_t. In both cases we don't want to fail. Instead,
                // we continue and 'make_literal()' will handling things properly.
                raw_u64 = 0;
            }

            double int_part = 0, frac_part = 0;
            frac_part = std::modf(raw_f64, &int_part);

            if (frac_part != 0)
            {
                *value = hrz::vector_data::attr_from<RawValue>(raw_f64);
            }

            std::optional<double> frac_part_opt = std::nullopt;
            if (pos >= 0)
            {
                frac_part_opt = {frac_part};
            }

            *value = make_number_literal(sign, raw_u64, int_part, frac_part_opt);
        }

        return Result{Result::Ok, 0};
    }

    Result parse_function(Lexer& lexer, Ast& ast, const Token& func_name_token, uint32_t* subtree)
        const
    {
        static const auto funcs_map = []()
        {
            hrz::flat_hash_map<std::string_view, Operator> funcs;
#define FUNC(EnumName, OpCount, Name) funcs.insert({Name, Operator::EnumName});
            HRZ_STYLE_DEFINE_FUNCS
#undef FUNC
            return funcs;
        }();

        auto it = funcs_map.find(func_name_token.span);
        if (it == funcs_map.end())
        {
            HRZ_LOG_ERROR(
                "Unknown function \"{}\" on line {}.", func_name_token.span, func_name_token.line);
            return Result{Result::UnknownKeyword, 0};
        }
        Operator func_operator = it->second;

        Token token;

        CHECK_ERR(lexer.next(&token));
        EXPECT_TOKEN(token, OpenParen);

        Ast::Node func_node(NodeKind::Operator);
        func_node.op.op = func_operator;
        func_node.op.operand_count = 0;

        do
        {
            if (token.kind == Token::Comma)
            {
                CHECK_ERR(lexer.next(&token));
            }

            CHECK_ERR(parse_expr_0(
                lexer, ast, &func_node.op.operand_exprs[func_node.op.operand_count++]));

            CHECK_ERR(lexer.peek(&token));
        } while (token.kind == Token::Comma && func_node.op.operand_count < kMaxFunctionParameters);

        *subtree = ast.nodes.size();
        ast.nodes.push_back(func_node);

        EXPECT_TOKEN(token, CloseParen);
        return Result{Result::Ok, 0};
    }

    std::string_view parse_string(std::string_view span, InternString& intern) const
    {
        if (span.find('\\') == std::string_view::npos)
        {
            return std::string_view(intern.intern(span), span.size());
        }

        // Parse escape sequences.
        std::string str;
        str.reserve(span.size());

        char utf8_bytes[4];
        for (uint32_t i = 0; i < span.size(); ++i)
        {
            if (span[i] == '\\')
            {
                switch (span[i + 1])
                {
                    case '"':
                        str.push_back('\"');
                        i++;
                        break;
                    case '\\':
                        str.push_back('\\');
                        i++;
                        break;
                    case 'n':
                        str.push_back('\n');
                        i++;
                        break;
                    case 't':
                        str.push_back('\t');
                        i++;
                        break;
                    case 'u':
                    {
#define EXPECT_CHAR_AT(INDEX, C)                                                        \
    if (i + (INDEX) >= span.size() || span[i + (INDEX)] != (C))                         \
    {                                                                                   \
        HRZ_LOG_WARNING("Unexpected character in Unicode sequence '{}'.", span[i + 1]); \
        str.push_back(span[i]);                                                         \
        break;                                                                          \
    }
                        EXPECT_CHAR_AT(2, '{');

                        size_t start = 3;
                        size_t end = start;
                        while (i + end < span.size() && hrz::is_ascii_hexdigit(span[i + end]))
                        {
                            end += 1;
                        }

                        EXPECT_CHAR_AT(end, '}');

                        uint32_t code_point{};
                        if (!absl::SimpleHexAtoi<uint32_t>(
                                span.substr(i + start, end - start), &code_point))
                        {
                            HRZ_LOG_WARNING("Invalid 32-bit Unicode code point");
                            str.push_back(span[i]);
                            break;
                        }

                        size_t bytes_written =
                            hrz::str::encode_code_point_to_utf8(code_point, utf8_bytes);
                        str.append(utf8_bytes, bytes_written);
                        i += end;
                        break;
#undef EXPECT_CHAR_AT
                    }
                    default:
                    {
                        HRZ_LOG_WARNING("Unsupported escape sequence \\{}", span[i + 1]);
                        str.push_back(span[i]);
                        break;
                    }
                }
            }
            else
            {
                str.push_back(span[i]);
            }
        }

        return std::string_view(intern.intern(str), str.size());
    }

    // Reads a literal without any modification to the AST. This differs from 'parse_literal' which
    // appends a node in the AST after reading a literal.
    Result read_literal(Lexer& lexer, InternString& intern, RawValue* literal) const
    {
        Token token;
        CHECK_ERR(lexer.next(&token));

        if (token.kind == Token::StringLiteral)
        {
            if (token.span.size() > UINT16_MAX)
            {
                return Result{Result::StringLiteralTooLarge, token.line};
            }

            *literal = vector_data::attr_from<RawValue>(parse_string(token.span, intern));
        }
        else if (token.kind == Token::ColorLiteral)
        {
            auto color = hrz::parse_color_string(token.span);
            if (!color.has_value())
            {
                return Result{Result::InvalidColorLiteral, token.line};
            }

            *literal = vector_data::attr_from<RawValue>((uint32_t)color.value());
        }
        else if (token.kind == Token::NumericLiteral)
        {
            CHECK_ERR(parse_numeric_value(token, literal));
        }
        else if (token.kind == Token::True || token.kind == Token::False)
        {
            *literal = vector_data::attr_from<RawValue>(token.kind == Token::True ? true : false);
        }
        else
        {
            return Result{Result::UnexpectedToken, token.line};
        }

        return Result{Result::Ok, 0};
    }

    Result parse_literal(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        RawValue literal;
        CHECK_ERR(read_literal(lexer, ast.intern, &literal));

        Ast::Node node(NodeKind::Literal);
        node.literal = literal;

        *subtree = ast.nodes.size();
        ast.nodes.push_back(node);

        return Result{Result::Ok, 0};
    }

    Result parse_expr_4(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        Token token;
        CHECK_ERR(lexer.peek(&token));

        if (token.kind == Token::OpenParen)
        {
            EXPECT_TOKEN(token, OpenParen);
            CHECK_ERR(parse_expr_0(lexer, ast, subtree));
            EXPECT_TOKEN(token, CloseParen);
        }
        else if (
            token.kind == Token::NumericLiteral || token.kind == Token::StringLiteral
            || token.kind == Token::ColorLiteral || token.kind == Token::True
            || token.kind == Token::False)
        {
            CHECK_ERR(parse_literal(lexer, ast, subtree));
        }
        else if (token.kind == Token::Attr)
        {
            CHECK_ERR(parse_attribute(lexer, ast, subtree));
        }
        else if (token.kind == Token::Uniform)
        {
            CHECK_ERR(parse_uniform(lexer, ast, subtree));
        }
        else if (token.kind == Token::Enum)
        {
            CHECK_ERR(parse_enum(lexer, ast, subtree));
        }
        else if (token.kind == Token::Property)
        {
            CHECK_ERR(parse_property(lexer, ast, subtree));
        }
        else if (token.kind == Token::FunctionIdentifier)
        {
            CHECK_ERR(parse_function(lexer, ast, token, subtree));

            assert(ast.nodes[*subtree].kind == NodeKind::Operator);

            // Check operand count.
            const auto& node = ast.nodes[*subtree];
            auto expected_operand_count = get_func_operand_count(node.op.op);
            if (expected_operand_count.has_value())
            {
                if (node.op.operand_count != expected_operand_count.value())
                {
                    return Result{Result::InvalidNumberOfArguments, token.line};
                }
            }
            else if (node.op.operand_count > kMaxFunctionParameters)
            {
                // Vararg, still limited in number of args.
                return Result{Result::InvalidNumberOfArguments, token.line};
            }

            // Handle special cases.
            if (node.op.op == Operator::Fmt)
            {
                if (node.op.operand_count < 1)
                {
                    return Result{Result::InvalidNumberOfArguments, token.line};
                }

                // Check that the first parameter is a constant literal.
                const auto& expr = ast.nodes[node.op.operand_exprs[0]];
                if (expr.kind != NodeKind::Literal)
                {
                    HRZ_LOG_ERROR("fmt() first argument must be constant");
                    return Result{Result::MismatchType, token.line};
                }
            }
            else if (node.op.op == Operator::Colorize)
            {
                // The first parameter is a string literal but it must be transformed into a palette
                // index.

                auto& palette_node = ast.nodes[node.op.operand_exprs[0]];
                if (palette_node.kind != NodeKind::Literal)
                {
                    HRZ_LOG_ERROR("colorize() first argument must be constant");
                    return Result{Result::MismatchType, token.line};
                }

                std::string_view palette_name = vector_data::attr_as_string(palette_node.literal);
                auto it = _palettes.find(hrz::murmur3_x64_128(palette_name));
                if (it == _palettes.end())
                {
                    return Result{Result::UnknownPalette, token.line};
                }
                else
                {
                    palette_node.kind = NodeKind::Palette;
                    palette_node.data.value = it->second;
                }
            }
        }
        else
        {
            return Result{Result::UnexpectedToken, token.line};
        }

        return Result{Result::Ok, 0};
    }

    Result parse_expr_3(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        Token token;
        CHECK_ERR(lexer.peek(&token));

        if (token.kind == Token::Not)
        {
            Ast::Node node(NodeKind::Operator);
            node.op.op = Operator::Not;
            node.op.operand_count = 1;

            CHECK_ERR(lexer.next(&token));
            CHECK_ERR(parse_expr_3(lexer, ast, &node.op.operand_exprs[0]));

            *subtree = ast.nodes.size();
            ast.nodes.push_back(node);
        }
        else
        {
            CHECK_ERR(parse_expr_4(lexer, ast, subtree));

            CHECK_ERR(lexer.peek(&token));
            if (is_comparison_operator(token.kind))
            {
                auto comparison_op = comparison_token_to_operator(token.kind);
                if (!comparison_op.has_value())
                {
                    return Result{Result::UnexpectedToken, token.line};
                }

                Ast::Node node(NodeKind::Operator);
                node.op.op = comparison_op.value();
                node.op.operand_count = 2;
                node.op.operand_exprs[0] = *subtree;

                CHECK_ERR(lexer.next(&token));
                CHECK_ERR(parse_expr_4(lexer, ast, &node.op.operand_exprs[1]));

                *subtree = ast.nodes.size();
                ast.nodes.push_back(node);
            }
        }

        return Result{Result::Ok, 0};
    }

    Result parse_expr_1(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        CHECK_ERR(parse_expr_3(lexer, ast, subtree));

        Token token;
        CHECK_ERR(lexer.peek(&token));
        while (token.kind == Token::And)
        {
            Ast::Node node(NodeKind::Operator);
            node.op.op = Operator::And;
            node.op.operand_count = 2;
            node.op.operand_exprs[0] = *subtree;

            EXPECT_TOKEN(token, And);
            CHECK_ERR(parse_expr_3(lexer, ast, &node.op.operand_exprs[1]));
            CHECK_ERR(lexer.peek(&token));

            *subtree = ast.nodes.size();
            ast.nodes.push_back(node);
        }

        return Result{Result::Ok, 0};
    }

    Result parse_expr_0(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        CHECK_ERR(parse_expr_1(lexer, ast, subtree));

        Token token;
        CHECK_ERR(lexer.peek(&token));

        while (token.kind == Token::Or)
        {
            Ast::Node node(NodeKind::Operator);
            node.op.op = Operator::Or;
            node.op.operand_count = 2;
            node.op.operand_exprs[0] = *subtree;

            EXPECT_TOKEN(token, Or);
            CHECK_ERR(parse_expr_1(lexer, ast, &node.op.operand_exprs[1]));
            CHECK_ERR(lexer.peek(&token));

            *subtree = ast.nodes.size();
            ast.nodes.push_back(node);
        }

        return Result{Result::Ok, 0};
    }

    Result parse_expr(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        CHECK_ERR(parse_expr_0(lexer, ast, subtree));
        return Result{Result::Ok, 0};
    }

    Result parse_discard_instruction(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        Token token;
        EXPECT_TOKEN(token, Discard);
        EXPECT_TOKEN(token, Semicolon);

        Ast::Node node(NodeKind::Discard);
        ast.nodes.push_back(node);
        *subtree = ast.nodes.size() - 1;

        return Result{Result::Ok, 0};
    }

    Result parse_set_instruction(Lexer& lexer, Ast& ast, uint32_t* subtree, bool* ignore_set)
    {
        Token token;
        EXPECT_TOKEN(token, Set);
        EXPECT_TOKEN(token, StringLiteral);

        auto it = _properties.find(hrz::murmur3_x64_128(token.span));
        if (it == _properties.end())
        {
            *ignore_set = true;
            // Be friendly when setting properties that aren't declared. When this happens, simply
            // skip the 'set' statement entirely like if was non-existant.
            HRZ_LOG_WARNING(
                "Unknown property \"{}\" ({}). Skipping 'set' statement.", token.span, token.line);
            while (token.kind != Token::Eof && token.kind != Token::Semicolon)
            {
                CHECK_ERR(lexer.next(&token));
            }
            return token.kind == Token::Eof ? Result{Result::UnexpectedEof, token.line}
                                            : Result{Result::Ok, 0};
        }
        else
        {
            *ignore_set = false;
        }

        Ast::Node node(NodeKind::Set);
        node.set.prp_id = it->second;
        node.set.next_instr = Ast::kInvalidNodeIndex;

        EXPECT_TOKEN(token, Eq);
        CHECK_ERR(parse_expr(lexer, ast, &node.set.expr));
        EXPECT_TOKEN(token, Semicolon);

        ast.nodes.push_back(node);
        *subtree = ast.nodes.size() - 1;

        return Result{Result::Ok, 0};
    }

    Result parse_emit_instruction(Lexer& lexer, Ast& ast, uint32_t* subtree) const
    {
        Token token;
        CHECK_ERR(lexer.peek(&token));
        EXPECT_TOKEN(token, Emit);

        Ast::Node node(NodeKind::Emit);

        CHECK_ERR(parse_expr(lexer, ast, &node.emit.expr));

        ast.nodes.push_back(node);
        *subtree = ast.nodes.size() - 1;

        EXPECT_TOKEN(token, Semicolon);
        return Result{Result::Ok, 0};
    }

    Result parse_block(Lexer& lexer, Ast& ast, uint32_t* root)
    {
        Token token;
        EXPECT_TOKEN(token, OpenBrace);
        CHECK_ERR(parse_instructions(lexer, ast, root));
        EXPECT_TOKEN(token, CloseBrace);
        return Result{Result::Ok, 0};
    }

    Result parse_fork_instruction(Lexer& lexer, Ast& ast, uint32_t* subtree)
    {
        Token token;
        EXPECT_TOKEN(token, Fork);

        Ast::Node node(NodeKind::Fork);
        node.fork.next_instr = Ast::kInvalidNodeIndex;
        CHECK_ERR(parse_block(lexer, ast, &node.fork.block));

        ast.nodes.push_back(node);
        *subtree = ast.nodes.size() - 1;

        return Result{Result::Ok, 0};
    }

    Result parse_branch_instruction(Lexer& lexer, Ast& ast, uint32_t* subtree)
    {
        Token token;
        bool first = true;
        bool has_else = false;

        uint32_t node_index = ast.nodes.size();
        ast.nodes.emplace_back();

        Ast::Node node(NodeKind::Branch);
        node.branch.next_instr = Ast::kInvalidNodeIndex;

        *subtree = node_index;

        while (!has_else)
        {
            bool should_have_condition = false;

            if (first)
            {
                EXPECT_TOKEN(token, If);
                should_have_condition = true;
                first = false;
            }
            else
            {
                CHECK_ERR(lexer.peek(&token));

                if (token.kind == Token::Elif)
                {
                    uint32_t elif_node_index = ast.nodes.size();
                    ast.nodes.emplace_back(NodeKind::Branch); // Elif node

                    node.branch.else_body = elif_node_index;
                    ast.nodes[node_index] = node;

                    node.branch.cond = Ast::kInvalidNodeIndex;
                    node.branch.then_body = Ast::kInvalidNodeIndex;
                    node.branch.else_body = Ast::kInvalidNodeIndex;
                    node.branch.next_instr = Ast::kInvalidNodeIndex;
                    node_index = elif_node_index;

                    should_have_condition = true;
                    CHECK_ERR(lexer.next(&token));
                }
                else if (token.kind == Token::Else)
                {
                    has_else = true;
                    CHECK_ERR(lexer.next(&token));
                }
                else
                {
                    node.branch.else_body = Ast::kInvalidNodeIndex;
                    break;
                }
            }

            if (should_have_condition)
            {
                EXPECT_TOKEN(token, OpenParen);
                CHECK_ERR(parse_expr(lexer, ast, &node.branch.cond));
                EXPECT_TOKEN(token, CloseParen);
            }

            if (has_else)
            {
                CHECK_ERR(parse_block(lexer, ast, &node.branch.else_body));
            }
            else
            {
                CHECK_ERR(parse_block(lexer, ast, &node.branch.then_body));
            }
        }

        ast.nodes[node_index] = node;

        return Result{Result::Ok, 0};
    }

    Result parse_instructions(Lexer& lexer, Ast& ast, uint32_t* subtree)
    {
        Token token;
        bool continue_parsing = true;
        bool ignore_instr = false;

        uint32_t prev_instr = Ast::kInvalidNodeIndex;
        uint32_t node_index{};
        while (continue_parsing)
        {
            CHECK_ERR(lexer.peek(&token));

            switch (token.kind)
            {
                case Token::Discard:
                    CHECK_ERR(parse_discard_instruction(lexer, ast, &node_index));
                    break;
                case Token::Emit: CHECK_ERR(parse_emit_instruction(lexer, ast, &node_index)); break;
                case Token::Set:
                    CHECK_ERR(parse_set_instruction(lexer, ast, &node_index, &ignore_instr));
                    if (ignore_instr) continue;
                    break;
                case Token::Fork: CHECK_ERR(parse_fork_instruction(lexer, ast, &node_index)); break;
                case Token::If: CHECK_ERR(parse_branch_instruction(lexer, ast, &node_index)); break;
                default:
                    continue_parsing = false;
                    node_index = Ast::kInvalidNodeIndex;
                    break;
            }

            if (prev_instr == Ast::kInvalidNodeIndex)
            {
                *subtree = node_index;
            }
            else
            {
                auto& prev_node = ast.nodes[prev_instr];
                switch (prev_node.kind)
                {
                    case NodeKind::Emit: prev_node.emit.next_instr = node_index; break;
                    case NodeKind::Discard: prev_node.discard.next_instr = node_index; break;
                    case NodeKind::Set: prev_node.set.next_instr = node_index; break;
                    case NodeKind::Fork: prev_node.fork.next_instr = node_index; break;
                    case NodeKind::Branch: prev_node.branch.next_instr = node_index; break;
                    default: assert(false && "Unhandled instruction"); break;
                }
            }

            prev_instr = node_index;
        }

        return Result{Result::Ok, 0};
    }

    Result parse(Lexer& lexer, Ast& ast) override
    {
        HRZ_SCOPED_SAMPLE("Parse styling script");

        CHECK_ERR(parse_instructions(lexer, ast, &ast.root));

        Token token;
        EXPECT_TOKEN(token, Eof);
        return Result{Result::Ok, 0};
    }
};
} // anonymous namespace

namespace hrz::style
{
std::unique_ptr<Parser> Parser::create()
{
    return std::make_unique<ParserImpl>();
}
} // namespace hrz::style
