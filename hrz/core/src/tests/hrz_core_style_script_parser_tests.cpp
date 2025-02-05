#include "hrz_core_style_script.h"

#include <hrz_fnd_defines.h>

#include <gsl/gsl-lite.hpp>
#include <gtest/gtest.h>

namespace
{
TEST(StyleScript, lex_real_script)
{
    static const char* str =
        ""
        "    \n"
        "    \n"
        "if (attr(\"type\") == \"tree\" and attr(\"height\") > -10.8) {\n"
        "    if (attr(\"distance to camera\") <= 0x3E8) {\n"
        "        set \"color\" = rgba(1.0, 0x2, 0.42, -6.2);\n"
        "        emit \"tree model\";\n"
        "    }\n"
        "    else {\n"
        "        emit \"tree model impostor\";\n"
        "    }\n\n"
        "}\n"
        "elif (not attr(\"type\") != \"light pole\" or attr(\"type\") == \"light_pole\") {\n"
        "    emit \"light pole model\";\n"
        "}\n"
        "else {\n"
        "    fork{discard;}\n"
        "}";

    auto lexer = hrz::style::Lexer::create(str);
    hrz::style::Token token{};

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::If);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Attr);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 3);
    EXPECT_EQ(token.span, "type");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::EqCmp);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 3);
    EXPECT_EQ(token.span, "tree");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::And);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Attr);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 3);
    EXPECT_EQ(token.span, "height");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::GtCmp);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    ASSERT_EQ(token.line, 3);
    EXPECT_EQ(token.span, "-10.8");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenBrace);
    ASSERT_EQ(token.line, 3);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::If);
    ASSERT_EQ(token.line, 4);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);
    ASSERT_EQ(token.line, 4);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Attr);
    ASSERT_EQ(token.line, 4);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);
    ASSERT_EQ(token.line, 4);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 4);
    EXPECT_EQ(token.span, "distance to camera");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);
    ASSERT_EQ(token.line, 4);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::LeqCmp);
    ASSERT_EQ(token.line, 4);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    ASSERT_EQ(token.line, 4);
    EXPECT_EQ(token.span, "0x3E8");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);
    ASSERT_EQ(token.line, 4);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenBrace);
    ASSERT_EQ(token.line, 4);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Set);
    ASSERT_EQ(token.line, 5);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 5);
    EXPECT_EQ(token.span, "color");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Eq);
    ASSERT_EQ(token.line, 5);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::FunctionIdentifier);
    ASSERT_EQ(token.line, 5);
    EXPECT_EQ(token.span, "rgba");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);
    ASSERT_EQ(token.line, 5);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    ASSERT_EQ(token.line, 5);
    ASSERT_EQ(token.span, "1.0");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Comma);
    ASSERT_EQ(token.line, 5);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    ASSERT_EQ(token.line, 5);
    ASSERT_EQ(token.span, "0x2");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Comma);
    ASSERT_EQ(token.line, 5);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    ASSERT_EQ(token.line, 5);
    ASSERT_EQ(token.span, "0.42");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Comma);
    ASSERT_EQ(token.line, 5);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    ASSERT_EQ(token.line, 5);
    ASSERT_EQ(token.span, "-6.2");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);
    ASSERT_EQ(token.line, 5);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Semicolon);
    ASSERT_EQ(token.line, 5);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Emit);
    ASSERT_EQ(token.line, 6);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 6);
    EXPECT_EQ(token.span, "tree model");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Semicolon);
    ASSERT_EQ(token.line, 6);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseBrace);
    ASSERT_EQ(token.line, 7);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Else);
    ASSERT_EQ(token.line, 8);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenBrace);
    ASSERT_EQ(token.line, 8);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Emit);
    ASSERT_EQ(token.line, 9);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 9);
    EXPECT_EQ(token.span, "tree model impostor");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Semicolon);
    ASSERT_EQ(token.line, 9);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseBrace);
    ASSERT_EQ(token.line, 10);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseBrace);
    ASSERT_EQ(token.line, 12);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Elif);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Not);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Attr);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 13);
    EXPECT_EQ(token.span, "type");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NeqCmp);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 13);
    EXPECT_EQ(token.span, "light pole");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Or);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Attr);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 13);
    EXPECT_EQ(token.span, "type");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::EqCmp);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 13);
    EXPECT_EQ(token.span, "light_pole");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenBrace);
    ASSERT_EQ(token.line, 13);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Emit);
    ASSERT_EQ(token.line, 14);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    ASSERT_EQ(token.line, 14);
    EXPECT_EQ(token.span, "light pole model");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Semicolon);
    ASSERT_EQ(token.line, 14);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseBrace);
    ASSERT_EQ(token.line, 15);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Else);
    ASSERT_EQ(token.line, 16);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenBrace);
    ASSERT_EQ(token.line, 16);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Fork);
    ASSERT_EQ(token.line, 17);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenBrace);
    ASSERT_EQ(token.line, 17);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Discard);
    ASSERT_EQ(token.line, 17);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Semicolon);
    ASSERT_EQ(token.line, 17);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseBrace);
    ASSERT_EQ(token.line, 17);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseBrace);
    ASSERT_EQ(token.line, 18);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Eof);
    ASSERT_EQ(token.line, 18);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Eof);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Eof);
}

TEST(StyleScript, lex_each_token)
{
    static const char* str =
        "    "
        " // This is a comment\n "
        " \"\" "
        " \"hello world\" "
        " 0 "
        " 15 "
        " -1 "
        " -15 "
        " -1.2 "
        " 78.94 "
        " .15 "
        " -.1 "
        " attr "
        " or and not "
        " emit discard fork set "
        " if elif else "
        " ; = == != < > <= >= "
        " (){} "
        " #123  "
        " #123f "
        " #aabbcc "
        " #aabbccff "
        " rgb rgba "
        " 0x0 "
        " 0xdeadbeef "
        " 0xdeadbeefaabbccdd "
        " -0x1 " // This is a valid lexing token but fails at parsing.
        " true false "
        " uniform "
        " enum "
        " prp ";

    auto lexer = hrz::style::Lexer::create(str);
    hrz::style::Token token{};

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    EXPECT_EQ(token.span, "");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::StringLiteral);
    EXPECT_EQ(token.span, "hello world");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "0");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "15");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "-1");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "-15");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "-1.2");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "78.94");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, ".15");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "-.1");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Attr);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Or);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::And);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Not);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Emit);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Discard);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Fork);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Set);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::If);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Elif);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Else);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Semicolon);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Eq);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::EqCmp);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NeqCmp);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::LtCmp);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::GtCmp);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::LeqCmp);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::GeqCmp);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenParen);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseParen);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::OpenBrace);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::CloseBrace);

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::ColorLiteral);
    EXPECT_EQ(token.span, "#123");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::ColorLiteral);
    EXPECT_EQ(token.span, "#123f");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::ColorLiteral);
    EXPECT_EQ(token.span, "#aabbcc");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::ColorLiteral);
    EXPECT_EQ(token.span, "#aabbccff");

    ASSERT_TRUE(lexer->next(&token));
    EXPECT_EQ(token.kind, hrz::style::Token::FunctionIdentifier);
    EXPECT_EQ(token.span, "rgb");

    ASSERT_TRUE(lexer->next(&token));
    EXPECT_EQ(token.kind, hrz::style::Token::FunctionIdentifier);
    EXPECT_EQ(token.span, "rgba");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "0x0");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "0xdeadbeef");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "0xdeadbeefaabbccdd");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::NumericLiteral);
    EXPECT_EQ(token.span, "-0x1");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::True);
    EXPECT_EQ(token.span, "true");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::False);
    EXPECT_EQ(token.span, "false");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Uniform);
    EXPECT_EQ(token.span, "uniform");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Enum);
    EXPECT_EQ(token.span, "enum");

    ASSERT_TRUE(lexer->next(&token));
    ASSERT_EQ(token.kind, hrz::style::Token::Property);
    EXPECT_EQ(token.span, "prp");
}

TEST(StyleScript, invalid_tokens)
{
    static const char* strs[] = {
        "\"",
        " \" ",
        ".",
        "-",
        "-5.",
        "-.",
        "6.",
        "===",
        "!==",
        "<<",
        ">>",
        "!",
        "#",
        "#3",
        "#4a",
        "#12c4e",
        "#a2b4c67",
        "#aabbccdd440",
        "0x",
        "-0x",
        "0x1122334455667788f",
        "-0x1122334455667788f",
    };

    static const int str_count = sizeof(strs) / sizeof(strs[0]);

    for (int i = 0; i < str_count; ++i)
    {
        SCOPED_TRACE(strs[i]);

        auto lexer = hrz::style::Lexer::create(strs[i]);
        hrz::style::Token token{};

        ASSERT_FALSE(lexer->next(&token));
    }
}

TEST(StyleScript, parse_discard)
{
    static const char* script = "discard; discard;";

    hrz::style::Ast ast;
    auto lexer = hrz::style::Lexer::create(script);
    auto parser = hrz::style::Parser::create();
    auto err = parser->parse(*lexer, ast);
    ASSERT_EQ(err.type, hrz::style::Result::Ok);
}

TEST(StyleScript, parse_emit)
{
    static const char* script = "emit \"tree\"; emit 0;";

    hrz::style::Ast ast;
    auto lexer = hrz::style::Lexer::create(script);
    auto parser = hrz::style::Parser::create();
    auto err = parser->parse(*lexer, ast);
    ASSERT_EQ(err.type, hrz::style::Result::Ok);
}

TEST(StyleScript, parse_invalid_emit)
{
    static const char* script = "emit;";

    hrz::style::Ast ast;
    auto lexer = hrz::style::Lexer::create(script);
    auto parser = hrz::style::Parser::create();
    auto err = parser->parse(*lexer, ast);
    ASSERT_EQ(err.type, hrz::style::Result::UnexpectedToken);
}

TEST(StyleScript, parse_set_invalid_syntax)
{
    static const char* scripts[] = {
        "set",
        "set = ;",
        "set 12 = 45;",
        "set \"hello\" = emit;",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "hello");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        EXPECT_FALSE(parser->parse(*lexer, ast));
    }
}

TEST(StyleScript, parse_set_unknown_property)
{
    static const char* script = "set \"size\" = 5;";

    hrz::style::Ast ast;
    auto lexer = hrz::style::Lexer::create(script);
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "sizei");
    parser->add_property(1, "siz");

    auto err = parser->parse(*lexer, ast);
    EXPECT_EQ(err.type, hrz::style::Result::Ok);
    EXPECT_EQ(err.line, 0);
}

TEST(StyleScript, parse_set_unknown_attribute)
{
    static const char* script = "set \"size\" = attr(\"hello\");";

    hrz::style::Ast ast;
    auto lexer = hrz::style::Lexer::create(script);
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "size");
    parser->add_attribute("hell");
    parser->add_attribute("hello2");

    auto err = parser->parse(*lexer, ast);
    ASSERT_EQ(err.type, hrz::style::Result::UnknownAttribute);
}

TEST(StyleScript, parse_unknown_palette)
{
    static const char* script = "set \"color\" = colorize(\"name_to_color\", attr(\"name\"));";

    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "color");
    parser->add_attribute("name");
    auto lexer = hrz::style::Lexer::create(script);
    auto err = parser->parse(*lexer, ast);
    EXPECT_EQ(err.type, hrz::style::Result::UnknownPalette);
    EXPECT_EQ(err.line, 1);
}

TEST(StyleScript, parse_set_unknown_uniform)
{
    static const char* script = "set \"color\" = hsl(uniform(\"tie_z\"), 0.5, 0.5);";

    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "color");
    parser->add_uniform("tile_z");

    auto lexer = hrz::style::Lexer::create(script);
    auto err = parser->parse(*lexer, ast);
    EXPECT_EQ(err.type, hrz::style::Result::UnknownUniform);
    EXPECT_EQ(err.line, 1);
}

TEST(StyleScript, parse_set_unknown_enum)
{
    static const char* script = "set \"alignment\" = enum(\"InvalidEnum\", \"CENTERED\");";

    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "alignment");

    auto lexer = hrz::style::Lexer::create(script);
    auto err = parser->parse(*lexer, ast);
    EXPECT_EQ(err.type, hrz::style::Result::UnknownEnum);
    EXPECT_EQ(err.line, 1);
}

TEST(StyleScript, parse_set_unknown_enum_value)
{
    static const char* script = "set \"alignment\" = enum(\"TextAlignment\", \"INVALID\");";

    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "alignment");

    auto lexer = hrz::style::Lexer::create(script);
    auto err = parser->parse(*lexer, ast);
    EXPECT_EQ(err.type, hrz::style::Result::UnknownEnumValue);
    EXPECT_EQ(err.line, 1);
}

TEST(StyleScript, parse_expr_fail)
{
    static const char* scripts[] = {
        "set \"u\" = colorize(attr(\"u\"), \"pu\");",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "u");
    parser->add_property(1, "s");
    parser->add_attribute("u");
    parser->add_attribute("s");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        EXPECT_FALSE(parser->parse(*lexer, ast));
        ast.reset();
    }
}

TEST(StyleScript, parse_set_typecheck_literal_ok)
{
    static const char* scripts[] = {
        "set \"u\" = 78;",         "set \"i\" = 79;",        "set \"i\" = -79;",
        "set \"d\" = 64;",         "set \"d\" = -12;",       "set \"d\" = 64.489;",
        "set \"d\" = -12.483;",    "set \"d\" = .489;",      "set \"d\" = -.483;",
        "set \"s\" = \"string\";", "set \"u\" = #12c;",      "set \"u\" = #b284;",
        "set \"u\" = #AABB38;",    "set \"u\" = #1eb825fF;", "set \"u\" = 0x02aB;",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "u");
    parser->add_property(1, "i");
    parser->add_property(2, "d");
    parser->add_property(3, "s");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        EXPECT_TRUE(parser->parse(*lexer, ast));
        ast.reset();
    }
}

TEST(StyleScript, parse_set_typecheck_expr_ok)
{
    static const char* scripts[] = {
        // Random
        "set \"i\" = rand_unif_i(-10, 10);",
        "set \"i\" = rand_unif_i(-10, attr(\"i\"));",
        "set \"u\" = rand_unif_u(0, 360);",
        "set \"u\" = rand_unif_u(0, attr(\"u\"));",
        "set \"i\" = rand_unif_u(0, attr(\"u\"));",
        "set \"d\" = rand_unif_f(-3.12, 360);",
        "set \"d\" = rand_unif_f(0, attr(\"u\"));",
        "set \"d\" = rand_unif_f(0, attr(\"i\"));",
        "set \"d\" = rand_unif_f(0, attr(\"d\"));",

        "set \"u\" = rand_norm_u(-5, 360.6);",
        "set \"u\" = rand_norm_u(0, attr(\"i\"));",
        "set \"i\" = rand_norm_u(0, attr(\"i\"));",
        "set \"d\" = rand_norm_f(-3.12, 360);",
        "set \"d\" = rand_norm_f(0, attr(\"u\"));",
        "set \"d\" = rand_norm_f(0, attr(\"i\"));",
        "set \"d\" = rand_norm_f(0, attr(\"d\"));",

        // Random (with implicit casts)
        "set \"d\" = rand_unif_i(-10, 10);",
        "set \"d\" = rand_norm_i(-10, 10);",
        "set \"d\" = rand_norm_i(-10, 10);",
        "set \"d\" = rand_unif_u(0, attr(\"u\"));",
        "set \"d\" = rand_norm_u(0, attr(\"i\"));",

        // Colorize
        "set \"u\" = colorize(\"pi\", attr(\"i\"));",
        "set \"u\" = colorize(\"pi\", -64);",
        "set \"u\" = colorize(\"pu\", attr(\"u\"));",
        "set \"u\" = colorize(\"pu\", 32);",
        "set \"u\" = colorize(\"pd\", attr(\"d\"));",
        "set \"u\" = colorize(\"pd\", 2.72);",
        "set \"u\" = colorize(\"ps\", attr(\"s\"));",
        "set \"u\" = colorize(\"ps\", \"string_literal\");",
        "set \"u\" = colorize(\"pd\", attr(\"i\"));",
        "set \"u\" = colorize(\"pd\", attr(\"u\"));",

        // Multiplication
        "set \"i\" = mul(attr(\"i\"), -3);",
        "set \"u\" = mul(attr(\"u\"), 3);",
        "set \"d\" = mul(attr(\"d\"), -3.14);",
        "set \"d\" = mul(mul(attr(\"d\"), 2.72), 3.14);",

        // Division
        "set \"i\" = div(attr(\"i\"), -3);",
        "set \"u\" = div(attr(\"u\"), 3);",
        "set \"d\" = div(attr(\"d\"), -3.14);",
        "set \"d\" = div(div(attr(\"d\"), 2.72), 3.14);",

        // Addition
        "set \"i\" = add(attr(\"i\"), -3);",
        "set \"u\" = add(attr(\"u\"), 3);",
        "set \"d\" = add(attr(\"d\"), -3.14);",
        "set \"d\" = add(add(attr(\"d\"), 2.72), 3.14);",

        // Substraction
        "set \"i\" = sub(attr(\"i\"), -3);",
        "set \"u\" = sub(attr(\"u\"), 3);",
        "set \"d\" = sub(attr(\"d\"), -3.14);",
        "set \"d\" = sub(sub(attr(\"d\"), 2.72), 3.14);",

        // Inverse
        "set \"u\" = inv(2);",
        "set \"u\" = inv(attr(\"u\"));",
        "set \"i\" = inv(attr(\"i\"));",
        "set \"d\" = inv(attr(\"d\"));",

        // Abs
        "set \"i\" = abs(-2);",
        "set \"u\" = abs(attr(\"u\"));",
        "set \"i\" = abs(attr(\"i\"));",
        "set \"d\" = abs(attr(\"d\"));",

        // Neg
        "set \"i\" = neg(-2);",
        "set \"u\" = neg(attr(\"u\"));",
        "set \"i\" = neg(attr(\"i\"));",
        "set \"d\" = neg(attr(\"d\"));",

        // Min
        "set \"i\" = min(attr(\"i\"), 3);",
        "set \"u\" = min(attr(\"u\"), 5);",
        "set \"d\" = min(attr(\"d\"), 7);",

        // Max
        "set \"i\" = max(attr(\"i\"), 3);",
        "set \"u\" = max(attr(\"u\"), 5);",
        "set \"d\" = max(attr(\"d\"), 7);",

        // Mod
        "set \"i\" = mod(10, 3);",
        "set \"i\" = mod(attr(\"i\"), 3);",
        "set \"u\" = mod(attr(\"u\"), 5);",
        "set \"d\" = mod(attr(\"d\"), 7);",

        // Round
        "set \"i\" = round(attr(\"i\"));",
        "set \"u\" = round(attr(\"u\"));",
        "set \"d\" = round(attr(\"d\"));",

        // Floor
        "set \"i\" = floor(attr(\"i\"));",
        "set \"u\" = floor(attr(\"u\"));",
        "set \"d\" = floor(attr(\"d\"));",

        // Ceil
        "set \"i\" = ceil(attr(\"i\"));",
        "set \"u\" = ceil(attr(\"u\"));",
        "set \"d\" = ceil(attr(\"d\"));",

        // Complex compositions
        "set \"u\" = colorize(\"pi\", mul(attr(\"i\"), -3));",
        "set \"i\" = mul(add(attr(\"i\"), 10), -1);",
        "set \"i\" = sub(add(div(attr(\"i\"), 2), 5), 1);",
        "set \"i\" = min(max(attr(\"i\"), 10), -10);",

        // Uniforms
        "set \"u\" = uniform(\"tile_z\");",
        "set \"d\" = uniform(\"tile_z\");",

        // Enums
        "set \"u\" = enum(\"TextAlignment\", \"CENTERED\");",
    };

    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "u");
    parser->add_property(1, "i");
    parser->add_property(2, "d");
    parser->add_property(3, "s");
    parser->add_attribute("u");
    parser->add_attribute("i");
    parser->add_attribute("d");
    parser->add_attribute("s");
    parser->add_palette("pi");
    parser->add_palette("pu");
    parser->add_palette("pd");
    parser->add_palette("ps");
    parser->add_uniform("tile_z");

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    hrz::style::Ast ast;

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        auto err = parser->parse(*lexer, ast);
        EXPECT_EQ(err.type, hrz::style::Result::Ok);
        EXPECT_EQ(err.line, 0);
        ast.reset();
    }
}

TEST(StyleScript, parse_set_typecheck_attribute_ok)
{
    static const char* scripts[] = {
        "set \"u\" = attr(\"u\");", "set \"i\" = attr(\"i\");", "set \"d\" = attr(\"u\");",
        "set \"d\" = attr(\"i\");", "set \"d\" = attr(\"d\");", "set \"s\" = attr(\"s\");",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "u");
    parser->add_property(1, "i");
    parser->add_property(2, "d");
    parser->add_property(3, "s");
    parser->add_attribute("u");
    parser->add_attribute("i");
    parser->add_attribute("d");
    parser->add_attribute("s");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        EXPECT_TRUE(parser->parse(*lexer, ast));
        ast.reset();
    }
}

TEST(StyleScript, parse_set_typecheck_color_func_ok)
{
    static const char* scripts[] = {
        "set \"u\" = rgb(1, 0.23, -1.6);",
        "set \"u\" = rgba(1.0, 123, -0.48, 5);",
        "set \"u\" = rgba(1.0, 123, -0.48, attr(\"u\"));",
        "set \"u\" = rgba(1.0, 123, -0.48, attr(\"i\"));",
        "set \"u\" = rgba(1.0, 123, -0.48, attr(\"d\"));",

        "set \"u\" = hsl(180, 1, 0.5);",
        "set \"u\" = hsla(180.0, 0.5, 0.5, 5);",
        "set \"u\" = hsla(180.0, 0.5, 0.5, attr(\"u\"));",
        "set \"u\" = hsla(180.0, 0.5, 0.5, attr(\"i\"));",
        "set \"u\" = hsla(180.0, 0.5, 0.5, attr(\"d\"));",

        "set \"u\" = invert_color(0x467891);",
        "set \"u\" = invert_color(attr(\"u\"));",
        "set \"u\" = invert_color(#ff0000ff);",
        "set \"u\" = invert_color(1);",

        "set \"u\" = rotate_hue(0x467891, 46);",
        "set \"u\" = rotate_hue(attr(\"u\"), attr(\"d\"));",
        "set \"u\" = rotate_hue(#ff0000ff, attr(\"d\"));",
        "set \"u\" = rotate_hue(1, add(attr(\"d\"), 45));",

        "set \"u\" = lighten(#f00f, 45.4);",
        "set \"u\" = lighten(attr(\"u\"), attr(\"d\"));",

        "set \"u\" = darken(#f00f, 45.4);",
        "set \"u\" = darken(attr(\"u\"), attr(\"d\"));",

        "set \"u\" = brighten(#f00f, 45.4);",
        "set \"u\" = brighten(attr(\"u\"), attr(\"d\"));",

        "set \"u\" = saturate(#f00f, 45.4);",
        "set \"u\" = saturate(attr(\"u\"), attr(\"d\"));",

        "set \"u\" = desaturate(#f00f, 45.4);",
        "set \"u\" = desaturate(attr(\"u\"), attr(\"d\"));",

        "set \"u\" = mix_colors(#f00, #00f, 0.5);",
        "set \"u\" = mix_colors(attr(\"u\"), #00f, attr(\"d\"));",
    };
    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);

    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "u");
    parser->add_attribute("u");
    parser->add_attribute("i");
    parser->add_attribute("d");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        EXPECT_EQ(parser->parse(*lexer, ast).type, hrz::style::Result::Ok);
        ast.reset();
    }
}

TEST(StyleScript, parse_fork)
{
    static const char* script = "fork { fork { discard; } discard; fork{} }";

    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    auto lexer = hrz::style::Lexer::create(script);
    EXPECT_EQ(parser->parse(*lexer, ast).type, hrz::style::Result::Ok);
}

TEST(StyleScript, parse_fork_fail)
{
    static const char* scripts[] = {
        "fork", "fork {", "fork {{}", "fork { fork }", "fork { fork }}", "fork { fork { }",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        EXPECT_FALSE(parser->parse(*lexer, ast));
        ast.reset();
    }
}

TEST(StyleScript, parse_branch)
{
    static const char* script =
        "if (not attr(\"d\") >= 15 and not (attr(\"s\") == \"hello\" or attr(\"s\") == \"HELLO\")) "
        "{\n"
        "    emit \"r\";\n"
        "} elif (attr(\"i\") == 1 or not attr(\"i\") != 2 or attr(\"i\") < 8) {\n"
        "} else { \n"
        "    if ((((attr(\"i\") < -8)))) {\n"
        "    } else {}\n"
        "    if (attr(\"i\") < 789.6) {\n"
        "    } elif (attr(\"u\") == 5) {}\n"
        "    if (not attr(\"i\") <= -789.6) {}\n"
        "}"
        "if (not not true) {}"
        "if (not false and true) {}"
        "if (0) {}"
        "if (1) {}"
        "if (-42) {}"
        "if (0.0) {}"
        "if (-1.2) {}"
        "if (\"chaussette\") {}"
        "if (\"\") {}"
        "if (attr(\"b\")) {}"
        "if (attr(\"i\")) {}"
        "if (attr(\"u\")) {}"
        "if (attr(\"d\")) {}"
        "if (attr(\"s\")) {}"
        "if (attr(\"b\") == true) {}"
        "if (attr(\"b\") == 0) {}"
        "if (attr(\"b\") == 1.5) {}"
        "if (true != false) {}";

    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_attribute("b");
    parser->add_attribute("u");
    parser->add_attribute("i");
    parser->add_attribute("d");
    parser->add_attribute("s");
    auto lexer = hrz::style::Lexer::create(script);
    auto err = parser->parse(*lexer, ast);
    EXPECT_EQ(err.type, hrz::style::Result::Ok);
    EXPECT_EQ(err.line, 0);
}

TEST(StyleScript, parse_branch_fail_control_flow)
{
    static const char* scripts[] = {
        "else {}",
        "elif (attr(\"a\") == 0) {}",
        "if {}",
        "if (attr(\"a\") == 0) {} elif {}",
        "if (attr(\"a\") == 0) {} else (attr(\"a\") == 0) {}",
        "if (attr(\"a\") == 0) {} else {",
        "if (attr(\"a\") == 0) {} else }",
        "if (attr(\"a\") == 0) { else {}",
        "if (attr(\"a\") == 0) { else",
        "if (attr(\"a\") == 0) else {}",
        "if (attr(\"a\") == 0) {} else {} elif (attr(\"a\") == 0) {}",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_attribute("a");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        EXPECT_FALSE(parser->parse(*lexer, ast));
        ast.reset();
    }
}

TEST(StyleScript, parse_branch_fail_condition_syntax)
{
    static const char* scripts[] = {
        "if () {}",
        "if (not) {}",
        "if (attr(\"u\") {}",
        "if (attr(\"u\") !=) {}",
        "if (attr(\"u\") not 45) {}",
        "if (attr(\"u\") == 1 or (attr(\"u\") >= 2) {}",
        "if (attr(\"u\") == 1 and) {}",
        "if (attr(\"u\") == 1 and not) {}",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_attribute("b");
    parser->add_attribute("u");
    parser->add_attribute("i");
    parser->add_attribute("d");
    parser->add_attribute("s");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        EXPECT_FALSE(parser->parse(*lexer, ast));
        ast.reset();
    }
}

TEST(StyleScript, parse_instructions_after_emit_and_discard)
{
    static const char* scripts[] = {
        "emit \"r\"; set \"color\" = #f0f; set \"color\" = #f0f;",
        "discard; set \"color\" = #f0f; set \"color\" = #f0f;",
    };

    static const size_t count = sizeof(scripts) / sizeof(scripts[0]);
    hrz::style::Ast ast;
    auto parser = hrz::style::Parser::create();
    parser->add_property(0, "color");

    for (size_t i = 0; i < count; ++i)
    {
        SCOPED_TRACE(scripts[i]);
        auto lexer = hrz::style::Lexer::create(scripts[i]);
        EXPECT_TRUE(parser->parse(*lexer, ast));
        ast.reset();
    }
}
} // namespace
