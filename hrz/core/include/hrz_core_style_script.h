#pragma once

#include <hrz_common_style.h>
#include <hrz_fnd_intern_string.h>

#include <memory>
#include <stdint.h>
#include <string_view>

namespace hrz::style
{
struct Token
{
    enum Kind
    {
        Eof,
        StringLiteral,
        NumericLiteral,
        ColorLiteral,
        Attr,
        Or,
        And,
        Not,
        Emit,
        Discard,
        Fork,
        If,
        Elif,
        Else,
        Set,
        Semicolon,
        Eq,
        EqCmp,
        NeqCmp,
        LtCmp,
        GtCmp,
        LeqCmp,
        GeqCmp,
        OpenParen,
        CloseParen,
        OpenBrace,
        CloseBrace,
        Comma,
        True,
        False,
        Uniform,
        Enum,
        Property,
        FunctionIdentifier,
    };

    Kind kind;
    int line;
    std::string_view span;
};

struct Result
{
    enum Type : uint8_t
    {
        Ok = 0,
        UnknownKeyword,
        UnknownToken,
        UnexpectedEof,
        UnexpectedToken,
        UnknownProperty,
        UnknownAttribute,
        UnknownUniform,
        UnknownEnum,
        UnknownEnumValue,
        UnknownPalette,
        MismatchType,
        StringLiteralTooLarge,
        NumericLiteralTooLarge,
        InvalidColorLiteral,
        InvalidNumericLiteral,
        InvalidNumberOfArguments,
    };

    Type type;
    int line;

    constexpr operator bool() const { return type == Ok; }

    const char* description() const
    {
        switch (type)
        {
            case Ok: return "Ok";
            case UnknownKeyword: return "Unknown keyword";
            case UnknownToken: return "Unknown token";
            case UnexpectedEof: return "Unexpected end of file";
            case UnexpectedToken: return "Unexpected token";
            case UnknownProperty: return "Unknown property";
            case UnknownAttribute: return "Unknown attribute";
            case UnknownUniform: return "Unknown uniform";
            case UnknownEnum: return "Unknown enum";
            case UnknownEnumValue: return "Unknown enum value";
            case UnknownPalette: return "Unknown palette";
            case MismatchType: return "Mismatch type";
            case StringLiteralTooLarge: return "String literal too large";
            case NumericLiteralTooLarge: return "Numeric literal too large";
            case InvalidColorLiteral: return "Invalid color literal";
            case InvalidNumericLiteral: return "Invalid numeric literal";
            case InvalidNumberOfArguments: return "Invalid number of arguments";
            default: return "Unknown error";
        }
    }
};

// Full styling script AST. This is an intermediate representation of a styling script produced by
// the parser. This version of the AST is meant to be easily manipulable. This AST is fed to some
// transformations phases that will turn it into its final version used during script evaluation.
struct Ast
{
    static constexpr uint32_t kInvalidNodeIndex = -1;

    struct Node
    {
        NodeKind kind = NodeKind::Unknown;

        union
        {
            RawValue literal;

            struct // Data
            {
                uint64_t value;
            } data;

            struct // Set
            {
                uint64_t prp_id;
                uint32_t expr;

                uint32_t next_instr;
            } set;

            struct // Emit
            {
                uint32_t expr;
                uint32_t next_instr;
            } emit;

            struct // Discard
            {
                uint32_t next_instr;
            } discard;

            struct // Branch
            {
                uint32_t cond;
                uint32_t then_body;
                uint32_t else_body;

                uint32_t next_instr;
            } branch;

            struct // Fork
            {
                uint32_t block;
                uint32_t next_instr;
            } fork;

            struct // For functions with more than 2 parameters.
            {
                Operator op;
                uint32_t operand_count;
                uint32_t operand_exprs[kMaxFunctionParameters];
            } op;
        };

        explicit Node(NodeKind kind_ = NodeKind::Unknown) : kind(kind_) {}
    };

    void reset()
    {
        intern.reset();
        nodes.clear();
    }

    hrz::InternString intern;
    uint32_t root;
    std::vector<Node> nodes;
};

struct Lexer
{
    static std::unique_ptr<Lexer> create(std::string_view input);

    virtual ~Lexer() = default;

    virtual Result next(Token* token) = 0;
    virtual Result peek(Token* token) = 0;
};

struct Parser
{
    static std::unique_ptr<Parser> create();

    virtual ~Parser() = default;

    enum : int
    {
        INSERT_ERROR = -1
    };

    enum : uint64_t
    {
        INVALID_PROPERTY = 0
    };

    virtual int add_attribute(std::string_view name) = 0;
    virtual void add_property(uint64_t id, std::string_view name) = 0;
    virtual void add_palette(std::string_view name) = 0;
    virtual int add_uniform(std::string_view name) = 0;

    virtual Result parse(Lexer& lexer, Ast&) = 0;
};

struct Optimizer
{
    static std::unique_ptr<Optimizer> create(gsl::span<const hrz::Palette> palettes);

    virtual ~Optimizer() = default;

    virtual bool optimize(Ast&&, FlatAst*) = 0;
};
} // namespace hrz::style
