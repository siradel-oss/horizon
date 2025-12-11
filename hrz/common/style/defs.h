#pragma once

#include <cstdint>
#include <optional>

namespace hrz::style
{

// The authoritative grammar for the styling language is defined in the
// styling API documentation. Don't forget to update it when making changes.

// Defines the maximum number of parameters a function can have.
static constexpr uint32_t kMaxFunctionParameters = 4;

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

bool is_random_function(Operator kind);

std::optional<uint8_t> get_func_operand_count(Operator kind);

} // namespace hrz::style
