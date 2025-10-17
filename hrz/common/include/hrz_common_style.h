#pragma once

#include "hrz_common_attributes.h"
#include "hrz_common_palette.h"
#include "hrz_common_random.h"
#include "hrz_common_vector_data.h"

#include <hrz_common_blob_array.h>
#include <hrz_fnd_arena.h>
#include <hrz_fnd_class.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_intern_string.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_maths.h>
#include <hrz_protocol_all.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// clang-format off
/*
This is the authoritative documentation for the styling scripts grammar.

Styling script grammar
=============================================

### Values

ESCAPED_CHAR    :=   '\n' | '\t' | '\\' | '\"' | '\u{' HEXDIGIT+ '}'
string_lit      :=   '"' (CHAR | ESCAPED_CHAR)* '"'
uint_lit        :=   ('0x' HEXDIGIT+) | DIGIT+
sint_lit        :=   '-'? DIGIT+
real_lit        :=   '-'? ('.' DIGIT+ | DIGIT+ ('.' DIGIT+))
numeric_lit     :=   uint_lit | sint_lit | real_lit
hex_color_lit   :=   '#' HEXDIGIT{3,4,6,8}
color_lit       :=   hex_color_digit
literal         :=   string_lit | numeric_lit | color_lit
attr            :=   'attr' '(' string_lit ')'

### Functions

function_name  :=    CHAR+
function_call  :=    function_name '(' expr (',' expr)* ')'
colorize_call  :=    'colorize' '(' string_lit ',' expr ')'   // Special case, ugh...
fmt_call       :=    'fmt' '(' string_lit ',' expr ')'   // Special case, ugh...
function       :=    function_call | colorize_call | fmt_call

### Expressions
expr        := expr_1 ('or' expr_1)*
expr_1      := expr_2 ('and' expr_3)*
expr_3      := 'not' expr_3 | expr_4 (('<' | '>' | '<=' | '>=' | '==' | '!=') expr_4)?
expr_4      := '(' expr ')' | function | literal | attr

### Instructions

block          :=   instr*
b_block        :=   '{' block '}'
instr          :=   discard | emit | fork | branch | set
discard        :=   'discard' ';'
emit           :=   'emit' expr ';'
fork           :=   'fork' b_block
branch         :=   branch_if (branch_elif)* (branch_else)?
branch_if      :=   'if' '(' expr ')' b_block
branch_elif    :=   'elif' '(' expr ')' b_block
branch_else    :=   'else' b_block
set            :=   'set' string_lit '=' expr ';'

*/
// clang-format on

namespace hrz::style
{
// Defines the maximum number of parameters a function can have.
static constexpr uint32_t kMaxFunctionParameters = 4;

using RawValue = vector_data::RefAttributeValue;

enum class NodeKind
{
    Unknown = 0,

    // Instructions
    Discard = 1,
    Emit,
    Set,
    Fork,
    Branch,

    // Values
    Literal,
    Attribute,
    Palette,
    Uniform,
    Property,

    // Special
    Operator,
    Expr,
    Block,
};

// enum name, operand/param count, style name
#define HRZ_STYLE_DEFINE_FUNCS             \
    FUNC(Mul, 2, "mul")                    \
    FUNC(Div, 2, "div")                    \
    FUNC(Add, 2, "add")                    \
    FUNC(Sub, 2, "sub")                    \
    FUNC(Inv, 1, "inv")                    \
    FUNC(Abs, 1, "abs")                    \
    FUNC(Neg, 1, "neg")                    \
    FUNC(Min, 2, "min")                    \
    FUNC(Max, 2, "max")                    \
    FUNC(Mod, 2, "mod")                    \
    FUNC(Round, 1, "round")                \
    FUNC(Floor, 1, "floor")                \
    FUNC(Ceil, 1, "ceil")                  \
    FUNC(Colorize, 2, "colorize")          \
    FUNC(Fmt, std::nullopt, "fmt")         \
    FUNC(Alpha, 2, "alpha")                \
    FUNC(Rgb, 3, "rgb")                    \
    FUNC(Rgba, 4, "rgba")                  \
    FUNC(Hsl, 3, "hsl")                    \
    FUNC(Hsla, 4, "hsla")                  \
    FUNC(RandUnifI, 2, "rand_unif_i")      \
    FUNC(RandUnifU, 2, "rand_unif_u")      \
    FUNC(RandUnifF, 2, "rand_unif_f")      \
    FUNC(RandNormI, 2, "rand_norm_i")      \
    FUNC(RandNormU, 2, "rand_norm_u")      \
    FUNC(RandNormF, 2, "rand_norm_f")      \
    FUNC(InvertColor, 1, "invert_color")   \
    FUNC(RotateHue, 2, "rotate_hue")       \
    FUNC(Lighten, 2, "lighten")            \
    FUNC(Brighten, 2, "brighten")          \
    FUNC(Darken, 2, "darken")              \
    FUNC(Saturate, 2, "saturate")          \
    FUNC(Desaturate, 2, "desaturate")      \
    FUNC(Lerp, 3, "lerp")                  \
    FUNC(MixColors, 3, "mix_colors")       \
    FUNC(MapboxTypeof, 1, "mapbox_typeof") \
    FUNC(ToInt, 1, "to_int")               \
    FUNC(ToUint, 1, "to_uint")             \
    FUNC(ToNumber, 1, "to_number")         \
    FUNC(ToString, 1, "to_string")         \
    FUNC(ToColor, 1, "to_color")           \
    FUNC(ToBoolean, 1, "to_boolean")       \
    FUNC(IsNull, 1, "is_null")             \
    FUNC(IsNan, 1, "is_nan")               \
    FUNC(ValueOr, 2, "value_or")

enum class Operator
{
    // Boolean operators
    And,
    Or,
    Not,

    // Comparison operators
    Eq,
    Neq,
    Gt,
    Lt,
    Geq,
    Leq,

// Functions
#define FUNC(EnumName, OperandCount, Name) EnumName,
    HRZ_STYLE_DEFINE_FUNCS
#undef FUNC
};

// Final representation of the styling script AST. Represents an AST that has been through
// optimization and simplification stages. It is the AST form that is fed to styling jobs.
struct FlatAst
{
    // This is a lightweight representation of a node. Because many nodes don't need to store any
    // data outside their type, we can use this simple node representation without the need of
    // allocating a 'FullNode'.
    // An index to an underlying 'FullNode' is added for nodes that require more information than
    // their type. This index can be recycled for nodes that store a single piece of information
    // (For instance, the 'Emit' node only need to store an index to the FullNode of an expression).
    // So, full nodes are allocated only when more than 32 bits of information is needed for a node.
    // Overall, this should improve data locality when processing the AST.
    struct NodeRef
    {
        NodeKind kind;

        union
        {
            // Node kinds having a full node representation are: 'Literal', 'Set', 'Branch',
            // 'Block', 'Expr'.
            uint32_t full_node;

            // Node kinds using the 'data' fields are:
            // - 'Attribute' -> attribute id
            // - 'Palette'   -> palette id
            // - 'Uniform'   -> uniform id
            // - 'Emit'      -> expression or literal node index
            // - 'Fork'      -> block node index
            // - 'Operator'  -> 16 MSB have operand count, 16 LSB have operator.
            uint32_t data;

            // Other node kinds don't use either of these fields which are set to 0.
        };

        constexpr uint32_t get_operator_operand_count() const { return data >> 16; }

        constexpr Operator get_operator_kind() const
        {
            return static_cast<Operator>(data & 0xFFFF);
        }

        constexpr void set_operator(Operator kind, uint32_t operand_count)
        {
            data = (operand_count << 16) | static_cast<uint32_t>(kind);
        }
    };

    struct FullNode
    {
        FullNode() : kind{NodeKind::Literal}, literal{} {}

        NodeKind kind{NodeKind::Literal};

        union
        {
            RawValue literal;

            struct
            {
                uint64_t id;
            } prp;

            struct
            {
                uint64_t prp_id;
                // Index to 'Expr' or 'Literal' node.
                uint32_t expr;
            } set;

            struct
            {
                // Index to 'Expr' node.
                uint32_t cond;
                // Indices to 'Block' nodes.
                uint32_t then_body;
                uint32_t else_body;
            } branch;

            struct
            {
                // Indices into a list of AST nodes [begin, end) where each node is an instruction.
                // This is also used for the root node of the AST.
                uint32_t begin;
                uint32_t end;
            } block;

            struct
            {
                // Represents the range of nodes to evaluate in 'expressions' array.
                uint32_t offset;
                uint32_t size;
            } expr;
        };
    };

    Arena arena{4096};
    hrz::InternString intern;

    uint32_t root;

    // Stores all the nodes of the AST.
    Arena::Vec<FullNode> full_nodes;
    // Stores references to statement nodes ('set', 'emit', 'if', etc). contiguously. This is used
    // to make the instructions properly ordered. Then, executing a block boils down to iterating
    // this array over the instruction span of a block.
    Arena::Vec<NodeRef> statements;
    // Expressions are flattened while building the AST. An expression is translated as a sequence
    // of nodes representing its reverse polish notation and then stored contiguously into this
    // array. AST nodes can then reference expressions by an offset and a size.
    Arena::Vec<NodeRef> expressions;

    inline uint32_t push_full_node(const FullNode& node)
    {
        arena.push(full_nodes, node);
        return full_nodes.size() - 1;
    }

    inline uint32_t push_statement(const NodeRef& node)
    {
        arena.push(statements, node);
        return statements.size() - 1;
    }

    inline uint32_t push_expression(const NodeRef& node)
    {
        arena.push(expressions, node);
        return expressions.size() - 1;
    }
};

struct OperatorEvaluator
{
    struct Context
    {
        std::span<const hrz::Palette> palettes;
        std::span<hrz::RngState> rng_states;
        hrz::Arena* arena;
    };

    bool operator()(
        Context& ctx,
        Operator op,
        std::span<const std::span<const RawValue>> arg_buffers,
        std::span<RawValue> res_buffer) const;
};

static bool is_random_function(Operator kind)
{
    return kind == Operator::RandUnifI || kind == Operator::RandUnifU || kind == Operator::RandUnifF
        || kind == Operator::RandNormI || kind == Operator::RandNormU
        || kind == Operator::RandNormF;
}

static constexpr std::optional<uint8_t> get_func_operand_count(Operator kind)
{
    switch (kind)
    {
#define FUNC(EnumName, OperandCount, Name) \
    case Operator::EnumName: return OperandCount;
        HRZ_STYLE_DEFINE_FUNCS
#undef FUNC
        default: assert(!"Unhandled case"); break;
    }

    return 0;
}

struct FeaturesStylingData
{
    struct Representation
    {
        uint32_t id{};
        std::string name;
    };

    // The AST is passed as a shared pointer to ensure it lives
    // at least as long as the job.
    std::shared_ptr<const FlatAst> ast;
    size_t feature_count;
    // In the same order than palettes were added to the parser.
    std::vector<hrz::Palette> palettes;
    std::vector<Representation> representations;
    hrz::flat_hash_map<uint32_t, vector_data::AttributeValues> attributes;
    hrz::flat_hash_map<uint32_t, vector_data::OwnedAttributeValue> uniforms;
    hrz::flat_hash_map<uint64_t, vector_data::OwnedAttributeValue> properties_default_values;

    uint64_t rng_seed;
    BlobArray<vector_data::FeatureIdHash> feature_ids_hashes;
};

struct StyledFeatures
{
    struct Instance
    {
        uint32_t feature_index;
        uint32_t repr_id;
        uint32_t first_prp; // Index in "prps" and "values"
        uint32_t prp_count;
    };

    BlobArray<Instance> instances;

    // All properties and their values are packed here. This is indexed by "first_prp"
    // and "prp_count" from each instance.
    // @Todo @Memory: An indirection array from interned property ID to a shorter ID could be used
    // to reduce the size of the IDs in this array.
    BlobArray<uint64_t> prps;
    BlobArray<vector_data::PackedAttributeValue> values;

    // All string packed here. Just like attributes.
    BlobArray<char> out_of_line_data;

    inline vector_data::PackedAttributeValuesReader get_values_reader() const
    {
        return vector_data::PackedAttributeValuesReader{
            values.get_cdata(), out_of_line_data.get_cdata()};
    }
};

struct StylingResult
{
    StyledFeatures features;

    // List of representations ids that have had at least one instance
    // generated.
    hrz::flat_hash_set<uint32_t> unique_reprs;
};

} // namespace hrz::style
