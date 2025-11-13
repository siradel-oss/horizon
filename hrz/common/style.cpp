#include "hrz/common/style.h"

#include "hrz/common/color.h"
#include "hrz/common/profiling.h"
#include "hrz/fnd/log.h"

#include <fmt/args.h>

#include <limits>

#define CHECK_ERR(...)      \
    do                      \
    {                       \
        if (!(__VA_ARGS__)) \
        {                   \
            return false;   \
        }                   \
    } while (0)

namespace
{
using namespace hrz;
using namespace style;
using namespace vector_data;

RawValue compute_numeric_palette_value(const hrz::Palette& palette, double value)
{
    auto color = hrz::palette::numeric_palettization(palette, value);
    assert(color.has_value());
    return attr_from_color<RawValue>(
        hrz::convert_rgba_color_to_bytes(hrz::linear_to_srgb(color.value())));
}

RawValue compute_label_palette_value(const hrz::Palette& palette, std::string_view label)
{
    auto color = hrz::palette::label_palettization(palette, label);
    assert(color.has_value());
    return attr_from_color<RawValue>(
        hrz::convert_rgba_color_to_bytes(hrz::linear_to_srgb(color.value())));
}

struct AndOp
{
    inline bool operator()(const RawValue& lhs, const RawValue& rhs, RawValue* res) const
    {
        *res = attr_as_bool(lhs) ? rhs : lhs; // Same semantics as JS
        return true;
    }
};

struct OrOp
{
    inline bool operator()(const RawValue& lhs, const RawValue& rhs, RawValue* res) const
    {
        *res = attr_as_bool(lhs) ? lhs : rhs; // Same semantics as JS
        return true;
    }
};

struct NotOp
{
    inline bool operator()(const RawValue& rhs, RawValue* res) const
    {
        *res = attr_from<RawValue>(!attr_as_bool(rhs));
        return true;
    }
};

template<typename Op>
struct ArithmeticBinaryOp
{
    inline bool operator()(const RawValue& lhs, const RawValue& rhs, RawValue* res) const
    {
        if (attr_is_string(lhs) || attr_is_string(rhs))
        {
            // One day we might want to do string + string, but for now, NaN everything!
            *res = attr_from<RawValue>(std::numeric_limits<double>::quiet_NaN());
        }
        else
        {
            Op op{};
            *res = attr_from<RawValue>(op(attr_as_number(lhs), attr_as_number(rhs)));
        }
        return true;
    }
};

struct MulOp
{
    inline RawValue operator()(double lhs, double rhs) const
    {
        return attr_from<RawValue>(lhs * rhs);
    }
};

struct DivOp
{
    inline RawValue operator()(double lhs, double rhs) const
    {
        if (rhs == 0)
        {
            return attr_from<RawValue>(
                (lhs >= 0) ? std::numeric_limits<double>::infinity()
                           : -std::numeric_limits<double>::infinity());
        }
        else
        {
            return attr_from<RawValue>(lhs / rhs);
        }
    }
};

struct AddOp
{
    inline RawValue operator()(double lhs, double rhs) const
    {
        return attr_from<RawValue>(lhs + rhs);
    }
};

struct SubOp
{
    inline RawValue operator()(double lhs, double rhs) const
    {
        return attr_from<RawValue>(lhs - rhs);
    }
};

struct MinOp
{
    inline RawValue operator()(double lhs, double rhs) const
    {
        return attr_from<RawValue>(lhs < rhs ? lhs : rhs);
    }
};

struct MaxOp
{
    inline RawValue operator()(double lhs, double rhs) const
    {
        return attr_from<RawValue>(lhs > rhs ? lhs : rhs);
    }
};

struct ModOp
{
    inline RawValue operator()(double lhs, double rhs) const
    {
        return rhs != 0 ? attr_from<RawValue>(lhs - rhs * std::floor(lhs / rhs))
                        : attr_from<RawValue>(std::numeric_limits<double>::quiet_NaN());
    }
};

template<typename Op>
struct ArithmeticUnaryOp
{
    inline bool operator()(const RawValue& v, RawValue* res) const
    {
        if (attr_is_string(v))
        {
            *res = attr_from<RawValue>(std::numeric_limits<double>::quiet_NaN());
        }
        else
        {
            Op op{};
            *res = attr_from<RawValue>(op(attr_as_number(v)));
        }
        return true;
    }
};

struct InvOp
{
    inline RawValue operator()(double rhs) const
    {
        return rhs != 0 ? attr_from<RawValue>(1.0 / rhs)
                        : attr_from<RawValue>(std::numeric_limits<double>::infinity());
    }
};

struct AbsOp
{
    inline RawValue operator()(double rhs) const
    {
        return attr_from<RawValue>((rhs < 0.0) ? -rhs : rhs);
    }
};

struct NegOp
{
    inline RawValue operator()(double rhs) const { return attr_from<RawValue>(-rhs); }
};

struct RoundOp
{
    inline RawValue operator()(double v) const { return attr_from<RawValue>(std::round(v)); }
};

struct FloorOp
{
    inline RawValue operator()(double v) const { return attr_from<RawValue>(std::floor(v)); }
};

struct CeilOp
{
    inline RawValue operator()(double v) const { return attr_from<RawValue>(std::ceil(v)); }
};

template<typename Op>
bool execute_op_functor_two_operands(
    const Op& op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(arg_buffers.size() == 2);
    auto lhs_buffer = arg_buffers[0];
    auto rhs_buffer = arg_buffers[1];
    assert(lhs_buffer.size() == rhs_buffer.size() && rhs_buffer.size() == res_buffer.size());

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        CHECK_ERR(op(lhs_buffer[i], rhs_buffer[i], &res_buffer[i]));
    }

    return true;
}

template<typename Op>
bool execute_op_functor_one_operand(
    const Op& op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(arg_buffers.size() == 1);
    auto rhs_buffer = arg_buffers[0];
    assert(rhs_buffer.size() == res_buffer.size());

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        CHECK_ERR(op(rhs_buffer[i], &res_buffer[i]));
    }

    return true;
}

bool execute_random_functions(
    OperatorEvaluator::Context& ctx,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(arg_buffers.size() == get_func_operand_count(op));

    auto arg_0 = arg_buffers[0];
    auto arg_1 = arg_buffers[1];

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        switch (op)
        {
            case Operator::RandUnifI:
            {
                int64_t low = attr_as_int64(arg_0[i]);
                int64_t up = attr_as_int64(arg_1[i]);
                res_buffer[i] =
                    attr_from<RawValue>(hrz::random::rand_i32(ctx.rng_states[i], low, up));
                break;
            }
            case Operator::RandUnifU:
            {
                uint64_t low = attr_as_uint64(arg_0[i]);
                uint64_t up = attr_as_uint64(arg_1[i]);
                res_buffer[i] =
                    attr_from<RawValue>(hrz::random::rand_u32(ctx.rng_states[i], low, up));
                break;
            }
            case Operator::RandUnifF:
            {
                double low = attr_as_number(arg_0[i]);
                double up = attr_as_number(arg_1[i]);
                res_buffer[i] =
                    attr_from<RawValue>(hrz::random::rand_f32(ctx.rng_states[i], low, up));
                break;
            }
            case Operator::RandNormI:
            {
                double mean = attr_as_number(arg_0[i]);
                double std = attr_as_number(arg_1[i]);
                res_buffer[i] =
                    attr_from<RawValue>(hrz::random::rand_norm_i32(ctx.rng_states[i], mean, std));
                break;
            }
            case Operator::RandNormU:
            {
                double mean = attr_as_number(arg_0[i]);
                double std = attr_as_number(arg_1[i]);
                res_buffer[i] =
                    attr_from<RawValue>(hrz::random::rand_norm_u32(ctx.rng_states[i], mean, std));
                break;
            }
            case Operator::RandNormF:
            {
                double mean = attr_as_number(arg_0[i]);
                double std = attr_as_number(arg_1[i]);
                res_buffer[i] =
                    attr_from<RawValue>(hrz::random::rand_norm_f32(ctx.rng_states[i], mean, std));
                break;
            }
            default: assert(!"Unhandled case"); return false;
        }
    }

    return true;
}

bool execute_color_functions(
    OperatorEvaluator::Context& ctx,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(arg_buffers.size() == get_func_operand_count(op));

    bool is_hsl = op == Operator::Hsl || op == Operator::Hsla;
    bool has_alpha = op == Operator::Rgba || op == Operator::Hsla;

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        lm::vec4 rgba;
        rgba.r = attr_as_number(arg_buffers[0][i]);
        rgba.g = attr_as_number(arg_buffers[1][i]);
        rgba.b = attr_as_number(arg_buffers[2][i]);
        rgba.a = has_alpha ? attr_as_number(arg_buffers[3][i]) : 1.0f;

        if (is_hsl)
        {
            rgba.r /= 360.0f;
            rgba.rgb = hsl_to_rgb(rgba.rgb);
        }

        res_buffer[i] = attr_from<RawValue>(convert_rgba_color_to_uint(rgba));
    }

    return true;
}

// @Todo(c++23) Use static operator()
struct SetAlphaColorModifier
{
    constexpr lm::vec4 operator()(lm::vec4 color, float mod) const { return {color.rgb, mod}; }
};

struct RotateHueColorModifier
{
    inline lm::vec4 operator()(lm::vec4 color, float hue_shift) const
    {
        color.rgb = rgb_to_hsv(color.rgb);
        color.x += hue_shift / 360.0F;
        color.rgb = hsv_to_rgb(color.rgb);
        return color;
    }
};

struct LightenColorModifier
{
    inline lm::vec4 operator()(lm::vec4 color, float mod) const
    {
        const float end = mod > 0.0F ? 1.0F : 0.0F;
        return lm::mix(color, lm::vec4(end, end, end, color.a), std::min(std::abs(mod), 1.0F));
    }
};

struct BrightenColorModifier
{
    inline lm::vec4 operator()(lm::vec4 color, float mod) const
    {
        return lm::vec4(color.rgb * std::max(0.0F, mod + 1.0F), color.a);
    }
};

struct DarkenColorModifier
{
    HRZ_NO_UNIQUE_ADDRESS LightenColorModifier inner{};

    inline lm::vec4 operator()(lm::vec4 color, float mod) const { return inner(color, -mod); }
};

struct SaturateColorModifier
{
    inline lm::vec4 operator()(lm::vec4 color, float mod) const
    {
        const float l = std::pow(
            lm::dot(lm::vec3(0.21F, 0.72F, 0.07F), srgb_to_linear(color.rgb)), 1.0F / 2.2F);
        return lm::vec4(lm::mix(lm::vec3(l), color.rgb, std::max(0.0F, mod + 1.0F)), color.a);
    }
};

struct DesaturateColorModifier
{
    HRZ_NO_UNIQUE_ADDRESS SaturateColorModifier inner{};

    inline lm::vec4 operator()(lm::vec4 color, float mod) const { return inner(color, -mod); }
};

template<typename Modifier>
bool execute_modify_color(
    OperatorEvaluator::Context& ctx,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(arg_buffers.size() == get_func_operand_count(op));
    Modifier modifier{};

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        lm::vec4 color = convert_uint_color_to_rgba((uint32_t)attr_as_uint64(arg_buffers[0][i]));
        const auto mod = (float)attr_as_number(arg_buffers[1][i]);
        color = modifier(color, mod);
        res_buffer[i] = attr_from<RawValue>(convert_rgba_color_to_uint(color));
    }

    return true;
}

bool execute_invert_color(
    OperatorEvaluator::Context& ctx,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(op == Operator::InvertColor);
    assert(arg_buffers.size() == get_func_operand_count(op));

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        // Always u64! Easy :)
        res_buffer[i] = attr_from<RawValue>((uint32_t)attr_as_uint64(arg_buffers[0][i]) ^ 0xffffff);
    }

    return true;
}

bool execute_lerp(
    OperatorEvaluator::Context& ctx,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(op == Operator::Lerp);
    assert(arg_buffers.size() == get_func_operand_count(op));

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        double from = attr_as_number(arg_buffers[0][i]);
        double to = attr_as_number(arg_buffers[1][i]);
        double t = attr_as_number(arg_buffers[2][i]);
        res_buffer[i] = attr_from<RawValue>(hrz::lerp(from, to, t));
    }

    return true;
}

bool execute_mix_colors(
    OperatorEvaluator::Context& ctx,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(op == Operator::MixColors);
    assert(arg_buffers.size() == get_func_operand_count(op));

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        lm::ubvec4 from = convert_uint_color_to_bytes((uint32_t)attr_as_uint64(arg_buffers[0][i]));
        lm::ubvec4 to = convert_uint_color_to_bytes((uint32_t)attr_as_uint64(arg_buffers[1][i]));
        double t = attr_as_number(arg_buffers[2][i]);

        uint64_t interpolated_color =
            convert_byte_color_to_uint(hrz::mix_srgb_colors_in_oklab(from, to, t));
        res_buffer[i] = attr_from<RawValue>(interpolated_color);
    }

    return true;
}

bool execute_mapbox_typeof(
    OperatorEvaluator::Context&,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(op == Operator::MapboxTypeof);
    assert(arg_buffers.size() == get_func_operand_count(op));

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        res_buffer[i] = attr_mapbox_typeof(arg_buffers[0][i]);
    }

    return true;
}

bool execute_transform(
    OperatorEvaluator::Context& ctx,
    Operator op,
    hrz_proto::AttributeTransform transform,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(arg_buffers.size() == get_func_operand_count(op));

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        res_buffer[i] = attr_transform<RefAttributeValue, RefAttributeValueArenaTraits>(
            transform, arg_buffers[0][i], *ctx.arena);
    }

    return true;
}

bool execute_is_null(
    OperatorEvaluator::Context&,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(op == Operator::IsNull);
    assert(arg_buffers.size() == get_func_operand_count(op));

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        res_buffer[i] = attr_from<RawValue>(attr_is_null(arg_buffers[0][i]));
    }

    return true;
}

bool execute_is_nan(
    OperatorEvaluator::Context&,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(op == Operator::IsNan);
    assert(arg_buffers.size() == get_func_operand_count(op));

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        auto value = arg_buffers[0][i];
        res_buffer[i] =
            attr_from<RawValue>(attr_is_null(value) || std::isnan(attr_as_number(value)));
    }

    return true;
}

bool execute_value_or(
    OperatorEvaluator::Context&,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(op == Operator::ValueOr);
    assert(arg_buffers.size() == get_func_operand_count(op));

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        const auto& value = arg_buffers[0][i];
        const auto& value_or = arg_buffers[1][i];
        res_buffer[i] = attr_is_null(value) ? value_or : value;
    }

    return true;
}

bool execute_colorize(
    OperatorEvaluator::Context& ctx,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(arg_buffers.size() == 2);

    // The palette is always constant.
    uint32_t palette_id = (uint32_t)attr_as_uint64(arg_buffers[0][0]);
    if (palette_id >= ctx.palettes.size())
    {
        HRZ_LOG_ERROR("Invalid palette ID during expression evaluation");
        return false;
    }
    const auto& palette = ctx.palettes[palette_id];

    auto rhs_buffer = arg_buffers[1];
    assert(rhs_buffer.size() == res_buffer.size());

    if (palette.type == hrz_proto::PaletteType::LABEL)
    {
        for (size_t i = 0; i < rhs_buffer.size(); ++i)
        {
            res_buffer[i] = compute_label_palette_value(palette, attr_as_string(rhs_buffer[i]));
        }
    }
    else if (palette.type == hrz_proto::PaletteType::NUMERIC)
    {
        for (size_t i = 0; i < rhs_buffer.size(); ++i)
        {
            res_buffer[i] = compute_numeric_palette_value(palette, attr_as_number(rhs_buffer[i]));
        }
    }
    else
    {
        assert(false && "Unhandled palette type");
        return false;
    }

    return true;
}

template<template<class> class Op>
bool execute_cmp_op(
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    assert(arg_buffers.size() == 2);
    auto lhs_buffer = arg_buffers[0];
    auto rhs_buffer = arg_buffers[1];
    assert(lhs_buffer.size() == rhs_buffer.size() && rhs_buffer.size() == res_buffer.size());

    for (size_t i = 0; i < res_buffer.size(); ++i)
    {
        const auto& a = lhs_buffer[i];
        const auto& b = rhs_buffer[i];
        bool res = false;
        if (attr_is_string(a) == attr_is_string(b) && attr_is_string(a))
        {
            Op<std::string_view> cmp_op;
            res = cmp_op(attr_as_string(a), attr_as_string(b));
        }
        else if (
            attr_is_64bit_integer(a) && attr_is_64bit_integer(b)
            && attr_is_uint64(a) == attr_is_uint64(b))
        {
            if (attr_is_uint64(a))
            {
                Op<uint64_t> cmp_op;
                res = cmp_op(attr_as_uint64(a), attr_as_uint64(b));
            }
            else
            {
                Op<int64_t> cmp_op;
                res = cmp_op(attr_as_int64(a), attr_as_int64(b));
            }
        }
        else
        {
            Op<double> cmp_op;
            res = cmp_op(attr_as_number(a), attr_as_number(b));
        }

        res_buffer[i] = attr_from<RawValue>(res);
    }

    return true;
}

bool execute_op(
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer)
{
    switch (op)
    {
        case Operator::Mul:
        {
            ArithmeticBinaryOp<MulOp> op{};
            CHECK_ERR(execute_op_functor_two_operands(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Div:
        {
            ArithmeticBinaryOp<DivOp> op{};
            CHECK_ERR(execute_op_functor_two_operands(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Add:
        {
            ArithmeticBinaryOp<AddOp> op{};
            CHECK_ERR(execute_op_functor_two_operands(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Sub:
        {
            ArithmeticBinaryOp<SubOp> op{};
            CHECK_ERR(execute_op_functor_two_operands(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Inv:
        {
            ArithmeticUnaryOp<InvOp> op{};
            CHECK_ERR(execute_op_functor_one_operand(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Abs:
        {
            ArithmeticUnaryOp<AbsOp> op{};
            CHECK_ERR(execute_op_functor_one_operand(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Neg:
        {
            ArithmeticUnaryOp<NegOp> op{};
            CHECK_ERR(execute_op_functor_one_operand(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Min:
        {
            ArithmeticBinaryOp<MinOp> op{};
            CHECK_ERR(execute_op_functor_two_operands(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Max:
        {
            ArithmeticBinaryOp<MaxOp> op{};
            CHECK_ERR(execute_op_functor_two_operands(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Mod:
        {
            ArithmeticBinaryOp<ModOp> op{};
            CHECK_ERR(execute_op_functor_two_operands(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Round:
        {
            ArithmeticUnaryOp<RoundOp> op{};
            CHECK_ERR(execute_op_functor_one_operand(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Floor:
        {
            ArithmeticUnaryOp<FloorOp> op{};
            CHECK_ERR(execute_op_functor_one_operand(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Ceil:
        {
            ArithmeticUnaryOp<CeilOp> op{};
            CHECK_ERR(execute_op_functor_one_operand(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Lt:
        {
            CHECK_ERR(execute_cmp_op<std::less>(arg_buffers, res_buffer));
            break;
        }
        case Operator::Gt:
        {
            CHECK_ERR(execute_cmp_op<std::greater>(arg_buffers, res_buffer));
            break;
        }
        case Operator::Leq:
        {
            CHECK_ERR(execute_cmp_op<std::less_equal>(arg_buffers, res_buffer));
            break;
        }
        case Operator::Geq:
        {
            CHECK_ERR(execute_cmp_op<std::greater_equal>(arg_buffers, res_buffer));
            break;
        }
        case Operator::Eq:
        {
            CHECK_ERR(execute_cmp_op<std::equal_to>(arg_buffers, res_buffer));
            break;
        }
        case Operator::Neq:
        {
            CHECK_ERR(execute_cmp_op<std::not_equal_to>(arg_buffers, res_buffer));
            break;
        }
        case Operator::Or:
        {
            OrOp op;
            CHECK_ERR(execute_op_functor_two_operands(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::And:
        {
            AndOp op;
            CHECK_ERR(execute_op_functor_two_operands(op, arg_buffers, res_buffer));
            break;
        }
        case Operator::Not:
        {
            NotOp op;
            CHECK_ERR(execute_op_functor_one_operand(op, arg_buffers, res_buffer));
            break;
        }
        default: assert(false && "Unhandled operation."); return false;
    }

    return true;
}
} // anonymous namespace

namespace hrz::style
{

bool is_random_function(Operator kind)
{
    return kind == Operator::RandUnifI || kind == Operator::RandUnifU || kind == Operator::RandUnifF
        || kind == Operator::RandNormI || kind == Operator::RandNormU
        || kind == Operator::RandNormF;
}

std::optional<uint8_t> get_func_operand_count(Operator kind)
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

bool OperatorEvaluator::operator()(
    Context& ctx,
    Operator op,
    std::span<const std::span<const RawValue>> arg_buffers,
    std::span<RawValue> res_buffer) const
{
    HRZ_SCOPED_SAMPLE_A("Evaluate style operator");

    switch (op)
    {
        case Operator::Colorize: CHECK_ERR(execute_colorize(ctx, arg_buffers, res_buffer)); break;
        case Operator::Fmt:
            // The fmt() operator is not implemented here and must be handled in the caller.
            assert(!"Unimplemented fmt() operator");
            break;
        case Operator::Rgb:
        case Operator::Rgba:
        case Operator::Hsl:
        case Operator::Hsla:
            CHECK_ERR(execute_color_functions(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::InvertColor:
            CHECK_ERR(execute_invert_color(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::Alpha:
            CHECK_ERR(
                execute_modify_color<SetAlphaColorModifier>(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::RotateHue:
            CHECK_ERR(
                execute_modify_color<RotateHueColorModifier>(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::Lighten:
            CHECK_ERR(execute_modify_color<LightenColorModifier>(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::Brighten:
            CHECK_ERR(
                execute_modify_color<BrightenColorModifier>(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::Darken:
            CHECK_ERR(execute_modify_color<DarkenColorModifier>(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::Saturate:
            CHECK_ERR(
                execute_modify_color<SaturateColorModifier>(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::Desaturate:
            CHECK_ERR(
                execute_modify_color<DesaturateColorModifier>(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::Lerp: CHECK_ERR(execute_lerp(ctx, op, arg_buffers, res_buffer)); break;
        case Operator::MixColors:
            CHECK_ERR(execute_mix_colors(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::MapboxTypeof:
            CHECK_ERR(execute_mapbox_typeof(ctx, op, arg_buffers, res_buffer));
            break;
        case Operator::ToInt:
            CHECK_ERR(execute_transform(
                ctx, op, hrz_proto::ATTRIBUTE_TRANSFORM_TO_INT, arg_buffers, res_buffer));
            break;
        case Operator::ToUint:
            CHECK_ERR(execute_transform(
                ctx, op, hrz_proto::ATTRIBUTE_TRANSFORM_TO_UINT, arg_buffers, res_buffer));
            break;
        case Operator::ToNumber:
            CHECK_ERR(execute_transform(
                ctx, op, hrz_proto::ATTRIBUTE_TRANSFORM_TO_NUMBER, arg_buffers, res_buffer));
            break;
        case Operator::ToString:
            CHECK_ERR(execute_transform(
                ctx, op, hrz_proto::ATTRIBUTE_TRANSFORM_TO_STRING, arg_buffers, res_buffer));
            break;
        case Operator::ToColor:
            CHECK_ERR(execute_transform(
                ctx, op, hrz_proto::ATTRIBUTE_TRANSFORM_TO_COLOR, arg_buffers, res_buffer));
            break;
        case Operator::ToBoolean:
            CHECK_ERR(execute_transform(
                ctx, op, hrz_proto::ATTRIBUTE_TRANSFORM_TO_BOOLEAN, arg_buffers, res_buffer));
            break;
        case Operator::IsNull: CHECK_ERR(execute_is_null(ctx, op, arg_buffers, res_buffer)); break;
        case Operator::IsNan: CHECK_ERR(execute_is_nan(ctx, op, arg_buffers, res_buffer)); break;
        case Operator::ValueOr:
            CHECK_ERR(execute_value_or(ctx, op, arg_buffers, res_buffer));
            break;
        default:
            if (is_random_function(op))
            {
                CHECK_ERR(execute_random_functions(ctx, op, arg_buffers, res_buffer));
            }
            else
            {
                CHECK_ERR(execute_op(op, arg_buffers, res_buffer));
            }
            break;
    }

    return true;
}

} // namespace hrz::style
