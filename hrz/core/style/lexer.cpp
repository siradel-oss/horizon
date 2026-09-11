// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/style/script.h"
#include "hrz/fnd/char_utils.h"

namespace
{

using namespace hrz;
using namespace style;

struct LexerImpl : public Lexer
{
    std::string _mem;
    std::string_view _input;
    int _line = 1;
    Token peeked;
    bool has_peeked = false;

    explicit LexerImpl(std::string_view input) : _mem(input.data(), input.size()), _input(_mem) {}

    std::string_view advance(size_t offset)
    {
        std::string_view ret(_input.data(), offset);
        _input = {_input.data() + offset, _input.size() - offset};
        return ret;
    }

    void skip_whitespaces()
    {
        size_t offset = 0;
        while (offset < _input.size() && hrz::is_ascii_whitespace(_input[offset]))
        {
            offset += 1;
        }

        for (unsigned int i = 0; i < offset; ++i)
        {
            if (_input[i] == '\n') _line += 1;
        }

        advance(offset);
    }

    Result skip_comments_and_whitespaces()
    {
        skip_whitespaces();

        while (_input.size() > 1 && _input[0] == '/')
        {
            if (_input[1] != '/')
            {
                return Result{Result::UnexpectedToken, _line};
            }

            size_t offset = 2;
            while (offset < _input.size() && _input[offset] != '\n')
            {
                offset += 1;
            }

            advance(offset);
            skip_whitespaces();
        }

        return Result{Result::Ok, 0};
    }

    std::string_view consume_word()
    {
        size_t offset = 0;
        while (offset < _input.size()
               && (hrz::is_ascii_alpha_numeric(_input[offset]) || _input[offset] == '_'))
        {
            offset += 1;
        }

        return advance(offset);
    }

    Result parse_keyword(Token* token)
    {
        Result err{Result::Ok, 0};
        auto kw = consume_word();
        assert(kw.size() > 0);

        static std::string_view keyword_if("if");
        static std::string_view keyword_elif("elif");
        static std::string_view keyword_else("else");
        static std::string_view keyword_attr("attr");
        static std::string_view keyword_or("or");
        static std::string_view keyword_and("and");
        static std::string_view keyword_not("not");
        static std::string_view keyword_emit("emit");
        static std::string_view keyword_enum("enum");
        static std::string_view keyword_discard("discard");
        static std::string_view keyword_fork("fork");
        static std::string_view keyword_set("set");
        static std::string_view keyword_true("true");
        static std::string_view keyword_false("false");
        static std::string_view keyword_uniform("uniform");
        static std::string_view keyword_prp("prp");

#define ACCEPT_KEYWORD(KW)       \
    do                           \
    {                            \
        token->kind = Token::KW; \
        token->span = kw;        \
        token->line = _line;     \
        return err;              \
    } while (0)

        switch (kw[0])
        {
            case 'a':
            {
                if (kw == keyword_attr)
                {
                    ACCEPT_KEYWORD(Attr);
                }
                else if (kw == keyword_and)
                {
                    ACCEPT_KEYWORD(And);
                }
                break;
            }
            case 'd':
            {
                if (kw == keyword_discard)
                {
                    ACCEPT_KEYWORD(Discard);
                }
                break;
            }
            case 'e':
            {
                if (kw == keyword_elif)
                {
                    ACCEPT_KEYWORD(Elif);
                }
                else if (kw == keyword_else)
                {
                    ACCEPT_KEYWORD(Else);
                }
                else if (kw == keyword_emit)
                {
                    ACCEPT_KEYWORD(Emit);
                }
                else if (kw == keyword_enum)
                {
                    ACCEPT_KEYWORD(Enum);
                }
                break;
            }
            case 'f':
            {
                if (kw == keyword_fork)
                {
                    ACCEPT_KEYWORD(Fork);
                }
                else if (kw == keyword_false)
                {
                    ACCEPT_KEYWORD(False);
                }
                break;
            }
            case 'i':
            {
                if (kw == keyword_if)
                {
                    ACCEPT_KEYWORD(If);
                }
                break;
            }
            case 'n':
            {
                if (kw == keyword_not)
                {
                    ACCEPT_KEYWORD(Not);
                }
                break;
            }
            case 'o':
            {
                if (kw == keyword_or)
                {
                    ACCEPT_KEYWORD(Or);
                }
                break;
            }
            case 'p':
            {
                if (kw == keyword_prp)
                {
                    ACCEPT_KEYWORD(Property);
                }
                break;
            }
            case 's':
            {
                if (kw == keyword_set)
                {
                    ACCEPT_KEYWORD(Set);
                }
                break;
            }
            case 't':
            {
                if (kw == keyword_true)
                {
                    ACCEPT_KEYWORD(True);
                }
                break;
            }
            case 'u':
            {
                if (kw == keyword_uniform)
                {
                    ACCEPT_KEYWORD(Uniform);
                }
                break;
            }
            default: break;
        }

        ACCEPT_KEYWORD(FunctionIdentifier);
    }

    Result parse_string_literal(Token* token)
    {
        Result err{Result::Ok, 0};

        assert(_input[0] == '"');
        advance(1);

        unsigned int offset = 0;
        while (offset < _input.size() && _input[offset] != '"')
        {
            if (_input[offset] == '\\') offset += 1;
            if (_input[offset] == '\n') _line++;
            offset += 1;
        }

        if (offset >= _input.size() || _input[offset] != '"')
        {
            err.type = Result::UnexpectedEof;
            err.line = _line;
            return err;
        }

        auto string = advance(offset);
        advance(1);

        token->kind = Token::StringLiteral;
        token->span = string;
        token->line = _line;
        return err;
    }

    bool is_cmp(char c) { return c == '=' || c == '<' || c == '>' || c == '!'; }

    Result parse_cmp(Token* token)
    {
        static std::string_view op_eq_cmp("==");
        static std::string_view op_neq_cmp("!=");
        static std::string_view op_leq_cmp("<=");
        static std::string_view op_geq_cmp(">=");

        Result err{Result::Ok, 0};

        unsigned int offset = 0;
        while (offset < _input.size() && is_cmp(_input[offset]))
        {
            offset += 1;
        }

        assert(offset > 0);
        bool found_op = false;
        auto op_span = advance(offset);

        switch (op_span.size())
        {
            case 1:
            {
                switch (op_span[0])
                {
                    case '=':
                        token->kind = Token::Kind::Eq;
                        found_op = true;
                        break;
                    case '<':
                        token->kind = Token::Kind::LtCmp;
                        found_op = true;
                        break;
                    case '>':
                        token->kind = Token::Kind::GtCmp;
                        found_op = true;
                        break;
                    default: break;
                }
                break;
            }
            case 2:
            {
                if (op_span == op_eq_cmp)
                {
                    token->kind = Token::Kind::EqCmp;
                    found_op = true;
                }
                else if (op_span == op_neq_cmp)
                {
                    token->kind = Token::Kind::NeqCmp;
                    found_op = true;
                }
                else if (op_span == op_leq_cmp)
                {
                    token->kind = Token::Kind::LeqCmp;
                    found_op = true;
                }
                else if (op_span == op_geq_cmp)
                {
                    token->kind = Token::Kind::GeqCmp;
                    found_op = true;
                }
                break;
            }
            default: break;
        }

        if (!found_op)
        {
            err.type = Result::UnknownToken;
            err.line = _line;
        }
        else
        {
            token->span = op_span;
            token->line = _line;
        }

        return err;
    }

    Result parse_color_literal(Token* token)
    {
        Result err{Result::Ok, 0};

        assert(_input[0] == '#');

        unsigned int offset = 1;

        while (offset < _input.size() && isxdigit(_input[offset]))
        {
            offset += 1;
        }

        if (offset == 1)
        {
            err.type = Result::Type::UnknownToken;
            err.line = _line;
            return err;
        }

        // Color hex is either #rgb, #rgba, #rrggbb or #rrggbbaa.
        if (offset != 4 && offset != 5 && offset != 7 && offset != 9)
        {
            err.type = Result::Type::UnknownToken;
            err.line = _line;
            return err;
        }

        token->kind = Token::Kind::ColorLiteral;
        token->span = advance(offset);
        token->line = _line;
        return err;
    }

    Result parse_numeric_literal(Token* token)
    {
        Result err{Result::Ok, 0};

        unsigned int offset = 0;

        if (_input[0] == '-')
        {
            offset += 1;
        }

        if (offset + 1 < _input.size() && _input[offset] == '0' && _input[offset + 1] == 'x')
        {
            // Lex hexadecimal literal.

            offset += 2;

            while (offset < _input.size() && isxdigit(_input[offset]))
            {
                offset += 1;
            }

            if (offset < 3 || (_input[0] == '-' && offset < 4))
            {
                return Result{Result::Type::UnknownToken, _line};
            }
            else if (offset > 18 || (_input[0] == '-' && offset > 19))
            {
                return Result{Result::Type::NumericLiteralTooLarge, _line};
            }
        }
        else
        {
            // Lex decimal literal.

            while (offset < _input.size() && hrz::is_ascii_digit(_input[offset]))
            {
                offset += 1;
            }

            if (offset < _input.size() && _input[offset] == '.')
            {
                bool has_fract = false;
                offset += 1;

                // If there is a point, there must be a fractional part.
                while (offset < _input.size() && hrz::is_ascii_digit(_input[offset]))
                {
                    offset += 1;
                    has_fract = true;
                }

                if (!has_fract)
                {
                    err.type = Result::Type::UnknownToken;
                    err.line = _line;
                    return err;
                }
            }

            assert(offset > 0);
            if (_input[0] == '-' && offset < 2)
            {
                err.type = Result::Type::UnknownToken;
                err.line = _line;
                return err;
            }
        }

        token->kind = Token::Kind::NumericLiteral;
        token->span = advance(offset);
        token->line = _line;
        return err;
    }

    Result parse_next(Token* token)
    {
        static std::string_view empty_span;
        Result err{Result::Ok, 0};

        err = skip_comments_and_whitespaces();
        if (err.type != Result::Ok)
        {
            return err;
        }

        if (_input.size() == 0)
        {
            token->kind = Token::Eof;
            token->line = _line;
            token->span = empty_span;
            return err;
        }

        char c = _input[0];
        if (hrz::is_ascii_letter(c))
        {
            return parse_keyword(token);
        }

        switch (c)
        {
            case '(':
            {
                token->kind = Token::OpenParen;
                token->span = advance(1);
                token->line = _line;
                return err;
            }
            case ')':
            {
                token->kind = Token::CloseParen;
                token->span = advance(1);
                token->line = _line;
                return err;
            }
            case '{':
            {
                token->kind = Token::OpenBrace;
                token->span = advance(1);
                token->line = _line;
                return err;
            }
            case '}':
            {
                token->kind = Token::CloseBrace;
                token->span = advance(1);
                token->line = _line;
                return err;
            }
            case ';':
            {
                token->kind = Token::Semicolon;
                token->span = advance(1);
                token->line = _line;
                return err;
            }
            case ',':
            {
                token->kind = Token::Comma;
                token->span = advance(1);
                token->line = _line;
                return err;
            }
            case '"': return parse_string_literal(token);
            case '=':
            case '!':
            case '>':
            case '<': return parse_cmp(token);
            case '#': return parse_color_literal(token);
            case '-':
            case '.':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9': return parse_numeric_literal(token);
            default: break;
        }

        err.type = Result::UnknownToken;
        err.line = _line;
        return err;
    }

    Result peek(Token* token) override
    {
        Result err{Result::Ok, 0};

        if (!has_peeked)
        {
            err = parse_next(&peeked);
            has_peeked = true;
        }

        *token = peeked;
        return err;
    }

    Result next(Token* token) override
    {
        if (has_peeked)
        {
            has_peeked = false;
            *token = peeked;
            return Result{Result::Ok, 0};
        }
        else
        {
            return parse_next(token);
        }
    }
};

} // namespace

namespace hrz::style
{

std::unique_ptr<Lexer> Lexer::create(std::string_view input)
{
    return std::unique_ptr<Lexer>(new LexerImpl(input));
}

} // namespace hrz::style
