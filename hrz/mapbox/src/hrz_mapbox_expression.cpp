#include "hrz_mapbox_expression.h"

#include "hrz_mapbox_common.h"

#include <hrz_common_color.h>
#include <hrz_fnd_json_utils.h>
#include <hrz_fnd_log.h>

#define DEBUG_PRINT_EXPRESSION_TREES 0

#define CHECK_OPERAND_COUNT(OpType)                                                \
    do                                                                             \
    {                                                                              \
        if (!check_operand_count((OpType), json.GetArray().Size() - 1))            \
        {                                                                          \
            HRZ_LOG_ERROR("Invalid operand count for operator ({})", (int)OpType); \
            return NO_NODE;                                                        \
        }                                                                          \
    } while (0)

#define CHECK_ERR(...)      \
    do                      \
    {                       \
        if (!(__VA_ARGS__)) \
        {                   \
            return NO_NODE; \
        }                   \
    } while (0)

namespace hrz_mapbox
{
using namespace hrz;

namespace
{
// List of Mapbox operators supported or partially supported.
// https://docs.mapbox.com/style-spec/reference/expressions/
enum class MapboxOp : uint8_t
{
    Unknown = 0,
    Get,
    Min,
    Max,
    Literal,
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    Abs,
    Round,
    Floor,
    Ceil,
    Interpolate,
    Step,
    Format,
    ToString,
    Not,
    Equal,
    NotEqual,
    Lesser,
    LesserOrEqual,
    Greater,
    GreaterOrEqual,
    All,
    Any,
    Case,
    Match,
    In,
};

bool check_operand_count(MapboxOp op, uint8_t operands)
{
    switch (op)
    {
        case MapboxOp::Get: return operands == 1;
        case MapboxOp::Min: return operands >= 2;
        case MapboxOp::Max: return operands >= 2;
        case MapboxOp::Literal: return operands == 1;
        case MapboxOp::Add: return operands >= 2;
        case MapboxOp::Subtract: return operands >= 1;
        case MapboxOp::Multiply: return operands >= 2;
        case MapboxOp::Divide: return operands == 2;
        case MapboxOp::Remainder: return operands == 2;
        case MapboxOp::Abs: return operands == 1;
        case MapboxOp::Round: return operands == 1;
        case MapboxOp::Floor: return operands == 1;
        case MapboxOp::Ceil: return operands == 1;
        case MapboxOp::Not: return operands == 1;
        case MapboxOp::Equal: return operands == 2;
        case MapboxOp::NotEqual: return operands == 2;
        case MapboxOp::Lesser: return operands == 2;
        case MapboxOp::LesserOrEqual: return operands == 2;
        case MapboxOp::Greater: return operands == 2;
        case MapboxOp::GreaterOrEqual: return operands == 2;
        // "all" and "any" appear without arguments in some styles, such as the MapLibre
        // "demotiles" style (https://demotiles.maplibre.org/)
        case MapboxOp::All: return true;
        case MapboxOp::Any: return true;
        default: HRZ_LOG_WARNING("Unknown or unsupported operator"); break;
    }

    return 0;
}

Node::Type mapbox_operator_to_node_type(MapboxOp op)
{
    switch (op)
    {
        case MapboxOp::Get: return Node::Type::Attribute;
        case MapboxOp::Min: return Node::Type::Min;
        case MapboxOp::Max: return Node::Type::Max;
        case MapboxOp::Literal: return Node::Type::Array;
        case MapboxOp::Add: return Node::Type::Add;
        case MapboxOp::Subtract: return Node::Type::Subtract;
        case MapboxOp::Multiply: return Node::Type::Multiply;
        case MapboxOp::Divide: return Node::Type::Divide;
        case MapboxOp::Remainder: return Node::Type::Remainder;
        case MapboxOp::Round: return Node::Type::Round;
        case MapboxOp::Floor: return Node::Type::Floor;
        case MapboxOp::Ceil: return Node::Type::Ceil;
        case MapboxOp::Abs: return Node::Type::Abs;
        case MapboxOp::Not: return Node::Type::Not;
        case MapboxOp::Equal: return Node::Type::Equal;
        case MapboxOp::NotEqual: return Node::Type::NotEqual;
        case MapboxOp::Lesser: return Node::Type::Lesser;
        case MapboxOp::LesserOrEqual: return Node::Type::LesserOrEqual;
        case MapboxOp::Greater: return Node::Type::Greater;
        case MapboxOp::GreaterOrEqual: return Node::Type::GreaterOrEqual;
        case MapboxOp::All: return Node::Type::And;
        case MapboxOp::Any: return Node::Type::Or;
        default: HRZ_LOG_WARNING("Unknown or unsupported operator"); break;
    }

    return (Node::Type)0;
}

} // anonymous namespace

NodeIndex parse_attribute(const rapidjson::Value& node, ExpressionContext& ctx)
{
    assert(!node.IsNull());

    if (node.IsArray())
    {
        HRZ_LOG_WARNING("Limited support: attributes names cannot be the result of an expression.");
        return NO_NODE;
    }

    auto str_opt = json::as_str(node);
    if (!str_opt.has_value())
    {
        HRZ_LOG_ERROR("Mismatch type, expected a string literal.");
        return NO_NODE;
    }

    const auto& attribute_name = str_opt.value();

    // Create the attribute.

    auto it = ctx.attributes->find(str_opt.value());
    if (it == ctx.attributes->end())
    {
        VectorSource::Attribute attr{};
        attr.id = ctx.attributes->size();

        ctx.attributes->insert({attribute_name, attr});
    }

    // Create sub-expression nodes.

    NodeIndex attribute_name_index = ctx.add_literal(Value::from(str_opt.value()));
    return ctx.add_node(Node::Type::Attribute, {attribute_name_index});
}

NodeIndex parse_literal(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    // https://docs.mapbox.com/style-spec/reference/types/

    switch (expected_type)
    {
        case MapboxPropertyType::Number:
        {
            auto value_opt = json::as_double(json);
            if (!value_opt.has_value())
            {
                HRZ_LOG_ERROR("Mismatch type, expected a float value.");
                return NO_NODE;
            }

            return ctx.add_literal(Value::from(value_opt.value()));
        }
        case MapboxPropertyType::Color:
        {
            auto str_opt = json::as_str(json);
            if (!str_opt.has_value())
            {
                HRZ_LOG_ERROR("Mismatch type, expected a string value.");
                return NO_NODE;
            }
            uint32_t color = 0;
            if (!hrz_mapbox::parse_mapbox_color_string(str_opt.value(), &color))
            {
                HRZ_LOG_ERROR("Failed to parse color string '{}'.", str_opt.value());
                return NO_NODE;
            }

            return ctx.add_literal(Value::from((uint64_t)color));
        }
        case MapboxPropertyType::Bool:
        {
            auto value_opt = json::as_bool(json);
            if (!value_opt.has_value())
            {
                HRZ_LOG_ERROR("Mismatch type, expected a boolean value.");
                return NO_NODE;
            }

            return ctx.add_literal(Value::from(value_opt.value()));
        }
        case MapboxPropertyType::String:
        case MapboxPropertyType::FormattedString:
        {
            auto value_opt = json::as_str(json);
            if (!value_opt.has_value())
            {
                HRZ_LOG_ERROR("Mismatch type, expected a string value.");
                return NO_NODE;
            }

            return ctx.add_literal(Value::from(value_opt.value()));
        }
        case MapboxPropertyType::Any:
        {
            // We have to guess the value type.
            if (json.IsString())
            {
                return ctx.add_literal(Value::from(json.GetString()));
                break;
            }

            if (json.IsBool())
            {
                return ctx.add_literal(Value::from(json.GetBool()));
                break;
            }

            if (json.IsNumber())
            {
                return ctx.add_literal(Value::from(json.GetDouble()));
            }

            assert(false && "Unrecognized literal");
        }
        default:
        {
            assert(false && "Cannot parse this as literal");
            return NO_NODE;
        }
    }
}

NodeIndex parse_array_literal(
    const rapidjson::Value& json,
    MapboxPropertyType expected_component_type,
    ExpressionContext& ctx)
{
    // https://docs.mapbox.com/style-spec/reference/expressions/#types-literal

    if (!json.IsArray())
    {
        HRZ_LOG_ERROR("Mismatch type, expected an array value.");
        return NO_NODE;
    }

    NodeIndex array_index = ctx.add_node(Node::Type::Array);

    for (size_t i = 0; i < json.GetArray().Size(); i++)
    {
        NodeIndex literal_index =
            parse_literal(hrz::json::get_nth_or_null(json, i), expected_component_type, ctx);
        if (literal_index == NO_NODE)
        {
            return NO_NODE;
        }

        ctx.nodes[array_index].children.push_back(literal_index);
    }

    return array_index;
}

enum class InterpolationOperator
{
    Interpolate,
    InterpolateHcl,
    InterpolateLab,
};

NodeIndex parse_interpolate_operator(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    InterpolationOperator interpolation_operator,
    ExpressionContext& ctx)
{
    // https://docs.mapbox.com/style-spec/reference/expressions/#interpolate
    auto operand_count = json.GetArray().Size() - 1;
    if (operand_count < 2)
    {
        HRZ_LOG_ERROR("Invalid operand count for \"interpolate\" operator");
        return NO_NODE;
    }

    auto& interpolation_json = json::get_nth_or_null(json, 1);
    if (!interpolation_json.IsArray() || interpolation_json.GetArray().Size() == 0)
    {
        HRZ_LOG_ERROR("Invalid interpolation type");
        return NO_NODE;
    }

    static const json::EnumVariant<InterpolationType> interpolation_types[] = {
        {"linear", InterpolationType::Linear},
        {"exponential", InterpolationType::Exponential},
        {"cubic-bezier", InterpolationType::CubicBezier},
    };

    auto interpolation_type = json::as_str_enum<InterpolationType>(
        json::get_nth_or_null(interpolation_json, 0), interpolation_types);

    if (interpolation_type != InterpolationType::Linear)
    {
        HRZ_LOG_INFO(
            "Non-linear interpolation isn't supported, using linear interpolation instead");
    }

    auto stop_parameter_count = operand_count - 2;

    if (stop_parameter_count % 2 != 0)
    {
        HRZ_LOG_ERROR("Invalid stop parameter count");
        return NO_NODE;
    }

    auto stop_count = stop_parameter_count / 2;

    auto& input_json = json::get_nth_or_null(json, 2);

    bool is_zoom_input = false;
    if (input_json.IsArray() && input_json.GetArray().Size() == 1)
    {
        auto input_operator_str = json::as_str(json::get_nth_or_null(input_json, 0));
        if (input_operator_str.has_value() && strcmp(input_operator_str.value(), "zoom") == 0)
        {
            is_zoom_input = true;
        }
    }

    if (is_zoom_input && stop_count == 2)
    {
        // Linear interpolation between two values, using the zoom level
        // as input. We can replace this with a constant, whose value is
        // the mean of the two interpolation extrema.
        // (Unless no mean value is defined, like for booleans and strings,
        // in which case the first value is used.)

        NodeIndex stop_output_1_index =
            parse_literal(json::get_nth_or_null(json, 4), expected_type, ctx);
        NodeIndex stop_output_2_index =
            parse_literal(json::get_nth_or_null(json, 6), expected_type, ctx);

        if (stop_output_1_index == NO_NODE || stop_output_2_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse output of interpolate operator");
            return NO_NODE;
        }

        Value literal;
        literal.type = ctx.nodes[stop_output_1_index].literal.type;

        switch (expected_type)
        {
            case MapboxPropertyType::Number:
                literal.f64 = (ctx.nodes[stop_output_1_index].literal.f64
                               + ctx.nodes[stop_output_2_index].literal.f64)
                    / 2;
                break;
            case MapboxPropertyType::Color:
            {
                auto first_color =
                    convert_uint_color_to_rgba(ctx.nodes[stop_output_1_index].literal.u64);
                auto second_color =
                    convert_uint_color_to_rgba(ctx.nodes[stop_output_2_index].literal.u64);
                auto mixed_color = interpolation_operator == InterpolationOperator::InterpolateHcl
                        || interpolation_operator == InterpolationOperator::InterpolateLab
                    ? mix_srgb_colors(first_color, second_color, 0.5f)
                    : lm::mix(first_color, second_color, 0.5f);
                literal.u64 = convert_rgba_color_to_uint(mixed_color);
            }
            break;
            default: HRZ_LOG_ERROR("Unsupported type for interpolation"); return NO_NODE;
        }

        // Remove the two literals that were used to compute the mean.
        assert(
            stop_output_1_index == ctx.nodes.size() - 2
            && stop_output_2_index == ctx.nodes.size() - 1);
        ctx.nodes.pop_back();
        ctx.nodes.pop_back();

        HRZ_LOG_INFO(
            "Unsupported linear interpolation based on the zoom level: replacing it with a mean "
            "value");

        return ctx.add_literal(literal);
    }
    else if (!is_zoom_input && expected_type == MapboxPropertyType::Color && stop_count >= 1)
    {
        // Generate a palette and colorize the input with it

        std::string palette_name = fmt::format("palette_{}", ctx.palettes.size() - 1);
        ctx.palettes.emplace_back();

        auto& palette = ctx.palettes.back();
        palette.set_type(hrz_proto::PaletteType::NUMERIC);
        palette.set_name(palette_name);
        auto* numeric_palette = palette.mutable_numeric();

        for (size_t i = 0; i < stop_count; ++i)
        {
            auto value = json::as_float(json::get_nth_or_null(json, 3 + i * 2 + 0));
            if (!value.has_value()) return NO_NODE;

            auto color_str = json::as_str(json::get_nth_or_null(json, 3 + i * 2 + 1));
            if (!color_str.has_value()) return NO_NODE;

            uint32_t color = 0;
            if (!parse_mapbox_color_string(color_str.value(), &color)) return NO_NODE;
            auto proto_color = convert_uint_to_proto_color(color);

            auto stop = numeric_palette->add_color_points();
            stop->set_value(value.value());
            *stop->mutable_first_color() = proto_color;
            *stop->mutable_second_color() = proto_color;
        }

        numeric_palette->set_interpolation_mode(
            interpolation_operator == InterpolationOperator::InterpolateHcl
                    || interpolation_operator == InterpolationOperator::InterpolateLab
                ? hrz_proto::ColorInterpolationMode::PERCEPTUAL_OKLAB
                : hrz_proto::ColorInterpolationMode::SRGB);

        NodeIndex input_index =
            parse_expression(json::get_nth_or_null(json, 2), MapboxPropertyType::Number, ctx);
        if (input_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse input of interpolate operator");
            return NO_NODE;
        }

        NodeIndex palette_name_node_index = ctx.add_literal(Value::from(palette_name));

        return ctx.add_node(Node::Type::Colorize, {palette_name_node_index, input_index});
    }
    else if (!is_zoom_input && stop_count == 2)
    {
        // Handle simple linear numerical interpolation

        // add(mul(div(sub(min(max(i, i1), i2), i1), sub(i2, i1)), sub(o2, o1)), o1)

        NodeIndex input_index = parse_expression(input_json, MapboxPropertyType::Number, ctx);
        if (input_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse input of interpolate operator");
            return NO_NODE;
        }

        NodeIndex stop_input_1_index =
            parse_literal(json::get_nth_or_null(json, 3), MapboxPropertyType::Number, ctx);
        NodeIndex stop_output_1_index =
            parse_literal(json::get_nth_or_null(json, 4), expected_type, ctx);
        NodeIndex stop_input_2_index =
            parse_literal(json::get_nth_or_null(json, 5), MapboxPropertyType::Number, ctx);
        NodeIndex stop_output_2_index =
            parse_literal(json::get_nth_or_null(json, 6), expected_type, ctx);

        if (stop_input_1_index == NO_NODE || stop_input_2_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse stop inputs of interpolate operator");
            return NO_NODE;
        }

        if (stop_output_1_index == NO_NODE || stop_output_2_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse stop outputs of interpolate operator");
            return NO_NODE;
        }

        NodeIndex max_node_index = ctx.add_node(Node::Type::Add, {input_index, stop_input_1_index});

        NodeIndex min_node_index =
            ctx.add_node(Node::Type::Min, {max_node_index, stop_input_2_index});

        NodeIndex sub1_node_index =
            ctx.add_node(Node::Type::Subtract, {min_node_index, stop_input_1_index});

        NodeIndex sub2_node_index =
            ctx.add_node(Node::Type::Subtract, {stop_input_2_index, stop_input_1_index});

        NodeIndex div_node_index =
            ctx.add_node(Node::Type::Divide, {sub1_node_index, sub2_node_index});

        NodeIndex sub3_node_index =
            ctx.add_node(Node::Type::Subtract, {stop_output_2_index, stop_output_1_index});

        NodeIndex mul_node_index =
            ctx.add_node(Node::Type::Multiply, {div_node_index, sub3_node_index});

        return ctx.add_node(Node::Type::Add, {mul_node_index, stop_output_1_index});
    }
    else
    {
        NodeIndex stop_output_1_index =
            parse_expression(json::get_nth_or_null(json, 4), expected_type, ctx);

        if (stop_output_1_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse stop outputs of interpolate operator");
            return NO_NODE;
        }

        HRZ_LOG_INFO(
            "Unsupported linear interpolation with more than two stops: replacing it with the "
            "value of the first stop");

        return stop_output_1_index;
    }
}

NodeIndex parse_step_operator(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    // https://docs.mapbox.com/style-spec/reference/expressions/#step
    auto operand_count = json.GetArray().Size() - 1;
    if (operand_count < 4 || operand_count % 2 != 0)
    {
        HRZ_LOG_ERROR("Invalid operand count for operator \"step\"");
        return NO_NODE;
    }

    auto stop_count = (operand_count - 2) / 2;

    if (expected_type == MapboxPropertyType::Color)
    {
        // Generate a palette and colorize the input with it

        std::string palette_name = fmt::format("palette_{}", ctx.palettes.size() - 1);
        ctx.palettes.push_back({});
        auto& palette = ctx.palettes.back();
        palette.set_type(hrz_proto::PaletteType::NUMERIC);
        palette.set_name(palette_name);
        auto* numeric_palette = palette.mutable_numeric();

        auto previous_color_str = json::as_str(json::get_nth_or_null(json, 2));
        if (!previous_color_str.has_value()) return NO_NODE;

        uint32_t previous_color = 0;
        if (!parse_mapbox_color_string(previous_color_str.value(), &previous_color)) return NO_NODE;
        auto previous_proto_color = convert_uint_to_proto_color(previous_color);

        for (size_t i = 0; i < stop_count; ++i)
        {
            auto value = json::as_float(json::get_nth_or_null(json, 3 + i * 2 + 0));
            if (!value.has_value()) return NO_NODE;

            auto color_str = json::as_str(json::get_nth_or_null(json, 3 + i * 2 + 1));
            if (!color_str.has_value()) return NO_NODE;

            uint32_t color = 0;
            if (!parse_mapbox_color_string(color_str.value(), &color)) return NO_NODE;
            auto proto_color = convert_uint_to_proto_color(color);

            auto stop = numeric_palette->add_color_points();
            stop->set_value(value.value());
            *stop->mutable_first_color() = previous_proto_color;
            *stop->mutable_second_color() = proto_color;

            previous_proto_color = proto_color;
        }

        numeric_palette->set_interpolation_mode(hrz_proto::ColorInterpolationMode::THRESHOLD);

        NodeIndex input_index =
            parse_expression(json::get_nth_or_null(json, 1), MapboxPropertyType::Number, ctx);
        if (input_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse input of step operator");
            return NO_NODE;
        }

        NodeIndex palette_name_node_index = ctx.add_literal(Value::from(palette_name));

        return ctx.add_node(Node::Type::Colorize, {palette_name_node_index, input_index});
    }
    else
    {
        NodeIndex stop_output_0_index =
            parse_expression(json::get_nth_or_null(json, 2), expected_type, ctx);

        if (stop_output_0_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse stop outputs of step operator");
            return NO_NODE;
        }

        HRZ_LOG_INFO(
            "Unsupported step operator, used for something other than a color: replacing it with "
            "the "
            "value of the first stop");

        return stop_output_0_index;
    }
}

NodeIndex parse_format_operator(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    // https://docs.mapbox.com/style-spec/reference/expressions/#interpolate
    NodeIndex operand_count = json.GetArray().Size() - 1;
    if (operand_count < 1)
    {
        HRZ_LOG_ERROR("Invalid operand count for operator \"format\"");
        return NO_NODE;
    }

    // Format strings are only allowed in specific places, and should not be able to substitute
    // any string property (both for Mapbox and Horizon).
    if (expected_type != MapboxPropertyType::FormattedString)
    {
        HRZ_LOG_ERROR("Expected type {} for operator \"format\"", (int)expected_type);
        return NO_NODE;
    }

    NodeIndex format_node_index = ctx.add_node(Node::Type::Format);
    NodeIndex format_string_node_index = ctx.add_literal(Value::from(""));

    ctx.nodes[format_node_index].children.push_back(format_string_node_index);

    // Operands should be given as a series of (string, options) pairs.
    // We use (operand_count + 1) because the options argument of the last pair is allowed to be
    // omitted.
    uint32_t format_argument_count = (operand_count + 1) / 2;
    std::string format_string = "";

    for (uint32_t i = 0; i < format_argument_count; ++i)
    {
        NodeIndex argument_node_index =
            parse_expression(json.GetArray()[i * 2 + 1], MapboxPropertyType::FormattedString, ctx);
        if (argument_node_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse argument to to-string operator");
            return NO_NODE;
        }

        if (ctx.nodes[argument_node_index].type == Node::Type::Literal)
        {
            // We can embed the value directly in the format string.
            // And we actually should, as we don't support literals in the "fmt" function.
            std::string literal_string = to_string(ctx.nodes[argument_node_index].literal);
            for (auto it = literal_string.begin(); it != literal_string.end(); ++it)
            {
                if (*it == '{' || *it == '}')
                {
                    it = literal_string.insert(it, *it);
                    it++;
                }
            }

            format_string += literal_string;
            ctx.nodes.pop_back();
        }
        else
        {
            // @Todo(HRZ-1016) Styling script functions only supports a maximum of 4 arguments for
            // now.
            if (ctx.nodes[format_node_index].children.size() >= 4)
            {
                HRZ_LOG_WARNING(
                    "Too many arguments for the Horizon string formatting function: strings will "
                    "be clipped.");
                break;
            }

            format_string += "{}";
            ctx.nodes[format_node_index].children.push_back(argument_node_index);
        }
    }

    assert(ctx.nodes[format_node_index].children.size() <= 4);

    ctx.string_data.push_front(format_string);
    ctx.nodes[format_string_node_index].literal.str = ctx.string_data.front();

    // Transform the node into a literal node if there is only the format string.
    if (ctx.nodes[format_node_index].children.size() == 1)
    {
        ctx.nodes[format_node_index] = ctx.nodes[format_string_node_index];
        assert(format_string_node_index == ctx.nodes.size() - 1);
        ctx.nodes.pop_back();
    }

    return format_node_index;
}

NodeIndex parse_to_string_operator(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    auto operand_count = json.GetArray().Size() - 1;
    if (operand_count != 1)
    {
        HRZ_LOG_ERROR("Invalid operand count for operator \"to-string\"");
        return NO_NODE;
    }

    if (expected_type != MapboxPropertyType::FormattedString)
    {
        HRZ_LOG_ERROR(
            "Got \"to-string\" operator while expecting an expression of type {}",
            (int)expected_type);
        return NO_NODE;
    }

    auto node = parse_expression(json.GetArray()[1], MapboxPropertyType::Any, ctx);
    return ctx.add_node(Node::Type::ToString, {node});
}

NodeIndex parse_typeof_operator(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    if (expected_type != MapboxPropertyType::String && expected_type != MapboxPropertyType::Any)
    {
        HRZ_LOG_ERROR(
            "Got \"typeof\" operator while expecting an expression of type {}", (int)expected_type);
        return NO_NODE;
    }

    auto operand_count = json.GetArray().Size() - 1;
    if (operand_count != 1)
    {
        HRZ_LOG_ERROR("Invalid operand count for operator \"typeof\", expected 1");
        return NO_NODE;
    }

    NodeIndex operand_index =
        parse_expression(json::get_nth_or_null(json, 1), MapboxPropertyType::Any, ctx);
    if (operand_index == NO_NODE)
    {
        HRZ_LOG_ERROR("Failed to parse operand of operator \"typeof\"");
        return NO_NODE;
    }

    // Since attributes are supposed to have a static type, we'll have to evaluate
    // this node once the type of the attribute has been inferred.
    // And if we cannot never infer it... we are in trouble!
    return ctx.add_node(Node::Type::Typeof, {operand_index});
}

// For such a layout:
//   PARENT
//   L_ CASE
//      L_ cond
//      L_ out0
//      L_ out1
//   L_ SIBLING
//
// We change it to:
//   CASE
//   L_ cond
//   L_ PARENT
//      L_ out0
//      L_ SIBLING
//   L_ PARENT
//      L_ out1
//      L_ SIBLING
void swap_case_node_with_parent(
    NodeIndex case_index,
    NodeIndex parent_index,
    ExpressionContext& ctx)
{
    assert(case_index < ctx.nodes.size() && parent_index < ctx.nodes.size());
    assert(ctx.nodes[case_index].type == Node::Type::Case);

    uint32_t index_of_case_in_parent = 0;
    bool found_case_in_parent = false;
    for (uint32_t i = 0; i < ctx.nodes[parent_index].children.size(); ++i)
    {
        if (ctx.nodes[parent_index].children[i] == case_index)
        {
            index_of_case_in_parent = i;
            found_case_in_parent = true;
            break;
        }
    }

    if (!found_case_in_parent)
    {
        assert(false && "Case node is not a child of parent node");
        return;
    }

    uint32_t branch_count = ctx.nodes[case_index].children.size() / 2 + 1;

    for (uint32_t branch_i = 0; branch_i < branch_count; ++branch_i)
    {
        // Create as many variants of the parent node as there are branches in the case node;
        uint32_t copy_index = ctx.copy_node(parent_index);

        // Replace the case node within that copy with the output of the case node for the
        // associated branch;
        uint32_t index_of_branch_in_case = branch_i * 2 + 1;
        if (branch_i == branch_count - 1)
        {
            // Last branch (the "else" branch) has no condition node, so the output node is one
            // node earlier than other branches
            index_of_branch_in_case--;
        }

        auto& copied_node = ctx.nodes.at(copy_index);
        copied_node.children[index_of_case_in_parent] =
            ctx.nodes[case_index].children[index_of_branch_in_case];

        // Replace the branch outcome node within the case node with the copy of the parent node.
        ctx.nodes[case_index].children[index_of_branch_in_case] = copy_index;
    }

    // Finally move the case node up in the tree.
    ctx.nodes[parent_index] = ctx.nodes[case_index];
}

// Finds the first case node within the subtree (depth first), swaps them with its parent node until
// it becomes the root node.
void bubble_up_case_node(NodeIndex root, ExpressionContext& ctx)
{
    assert(root < ctx.nodes.size());
    assert(ctx.nodes[root].type != Node::Type::Case);

    for (auto child_index : ctx.nodes[root].children)
    {
        if (ctx.nodes[child_index].type == Node::Type::Case)
        {
            swap_case_node_with_parent(child_index, root, ctx);
            return;
        }

        bubble_up_case_node(child_index, ctx);
    }
}

// Finds case nodes that live within the conditions of the given case node, bubbles them
// up to be the root node of each corresponding condition, then swaps them with the root
// case so that there are no remaining case node within the case conditions, but only within
// the case outputs, which is much easier to handle when generating styling scripts.
void collapse_case_conditions(NodeIndex case_index, ExpressionContext& ctx)
{
    assert(case_index < ctx.nodes.size());
    assert(ctx.nodes[case_index].type == Node::Type::Case);

    uint32_t condition_count = ctx.nodes[case_index].children.size() / 2;

    for (uint32_t i = 0; i < condition_count; ++i)
    {
        NodeIndex condition_index = ctx.nodes[case_index].children[i * 2];

        if (ctx.nodes[condition_index].type != Node::Type::Case)
        {
            bubble_up_case_node(condition_index, ctx);
        }

        // Nodes may have changed.
        if (ctx.nodes[condition_index].type == Node::Type::Case)
        {
            swap_case_node_with_parent(condition_index, case_index, ctx);

            // We have swapped the original root case with the condition case.
            // The original root case now lives within the outputs of the condition case, which
            // now occupies the case_index position.
            // But there may be MORE case nodes within the OTHER conditions of the original
            // root case.
            // They should not escape our watch!

            uint32_t output_count = ctx.nodes[case_index].children.size() / 2 + 1;
            for (uint32_t i = 0; i < output_count - 1; ++i)
            {
                NodeIndex output_index = ctx.nodes[case_index].children[i * 2 + 1];
                if (ctx.nodes[output_index].type == Node::Type::Case)
                {
                    collapse_case_conditions(output_index, ctx);
                }
            }

            NodeIndex default_output_index = ctx.nodes[case_index].children.back();
            if (ctx.nodes[default_output_index].type == Node::Type::Case)
            {
                collapse_case_conditions(default_output_index, ctx);
            }

            // Start from scratch at the same index, because the node may have been swapped
            // with an uncollapsed case node.
            // And the condition_count is not valid anymore, so we need to exit this invocation.
            collapse_case_conditions(case_index, ctx);
            return;
        }
    }
}

NodeIndex parse_case_operator(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    auto operand_count = json.GetArray().Size() - 1;
    if (operand_count < 3)
    {
        HRZ_LOG_ERROR("Invalid operand count for operator \"case\", expected at least 3");
        return NO_NODE;
    }

    if (operand_count % 2 == 0)
    {
        HRZ_LOG_ERROR("Invalid operand count for operator \"case\", default case missing");
        return NO_NODE;
    }

    NodeIndex case_index = ctx.add_node(Node::Type::Case);

    auto branch_count = operand_count / 2;
    for (uint32_t i = 0; i < branch_count; ++i)
    {
        const auto& condition_json = hrz::json::get_nth_or_null(json, i * 2 + 1);
        assert(!condition_json.IsNull());

        NodeIndex condition_index = parse_expression(condition_json, MapboxPropertyType::Bool, ctx);
        if (condition_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse condition {} of operator \"case\"", i);
            return NO_NODE;
        }

        const auto& output_json = hrz::json::get_nth_or_null(json, i * 2 + 2);
        assert(!output_json.IsNull());

        NodeIndex output_index = parse_expression(output_json, expected_type, ctx);
        if (output_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse output {} of operator \"case\"", i);
            return NO_NODE;
        }

        ctx.nodes[case_index].children.push_back(condition_index);
        ctx.nodes[case_index].children.push_back(output_index);
    }

    const auto& fallback_json = json::get_nth_or_null(json, json.GetArray().Size() - 1);
    assert(!fallback_json.IsNull());

    NodeIndex fallback_index = parse_expression(fallback_json, expected_type, ctx);
    if (fallback_index == NO_NODE)
    {
        HRZ_LOG_ERROR("Failed to parse fallback of operator operator \"case\"");
        return NO_NODE;
    }

    ctx.nodes[case_index].children.push_back(fallback_index);

    collapse_case_conditions(case_index, ctx);

    return case_index;
}

NodeIndex parse_match_operator(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    auto operand_count = json.GetArray().Size() - 1;
    if (operand_count < 4)
    {
        HRZ_LOG_ERROR("Invalid operand count for operator \"match\", expected at least 4");
        return NO_NODE;
    }

    if (operand_count % 2 > 0)
    {
        HRZ_LOG_ERROR("Invalid operand count for operator \"match\", default case missing");
        return NO_NODE;
    }

    NodeIndex case_index = ctx.add_node(Node::Type::Case);

    auto guess_input_type = [](const rapidjson::Value& value) -> std::optional<MapboxPropertyType>
    {
        if (value.IsNumber())
        {
            return MapboxPropertyType::Number;
        }
        else if (value.IsString())
        {
            return MapboxPropertyType::String;
        }
        else
        {
            return std::nullopt;
        }
    };

    // We first parse the label values because it gives us the information on the type of the input
    // value, which will enable us to assign the correct type to the attributes it uses.
    MapboxPropertyType input_type;
    {
        const auto& label_json = hrz::json::get_nth_or_null(json, 2);
        assert(!label_json.IsNull());

        const rapidjson::Value* label_value_json = &label_json;
        if (label_json.IsArray())
        {
            if (label_json.GetArray().Size() == 0)
            {
                HRZ_LOG_ERROR(
                    "Array literal used as label for operator \"match\" must not be empty");
                return NO_NODE;
            }

            label_value_json = &label_json.GetArray()[0];
        }

        auto input_type_opt = guess_input_type(*label_value_json);
        if (!input_type_opt.has_value())
        {
            HRZ_LOG_ERROR(
                "Labels for operator \"match\" must be a literal float, literal string, or array "
                "of "
                "literal floats or strings");
            return NO_NODE;
        }

        input_type = input_type_opt.value();
    }

    // Parse the input value
    const auto& input_value_json = hrz::json::get_nth_or_null(json, 1);
    assert(!input_value_json.IsNull());

    NodeIndex input_value_index = parse_expression(input_value_json, input_type, ctx);
    if (input_value_index == NO_NODE)
    {
        HRZ_LOG_ERROR("Failed to parse input value of operator \"match\"");
        return NO_NODE;
    }

    // Parses a label, and creates a node comparing the label to the input value
    auto parse_label_comparison = [&](const rapidjson::Value& json) -> NodeIndex
    {
        auto type_opt = guess_input_type(json);
        if (!type_opt.has_value() || type_opt.value() != input_type)
        {
            HRZ_LOG_ERROR("Mismatched types for label values of operator \"match\"");
            return NO_NODE;
        }

        NodeIndex label_index = parse_literal(json, type_opt.value(), ctx);
        if (label_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse label of operator \"match\"");
            return NO_NODE;
        }

        return ctx.add_node(Node::Type::Equal, {input_value_index, label_index});
    };

    // Parse the labels and outputs
    auto branch_count = (operand_count - 1) / 2;
    for (uint32_t i = 0; i < branch_count; ++i)
    {
        // Parse the label
        const auto& label_json = hrz::json::get_nth_or_null(json, (i + 1) * 2);
        assert(!label_json.IsNull());

        if (label_json.IsArray())
        {
            NodeIndex or_index = ctx.add_node(Node::Type::Or);

            for (size_t i = 0; i < label_json.GetArray().Size(); ++i)
            {
                NodeIndex comparison_index = parse_label_comparison(label_json.GetArray()[i]);
                if (comparison_index == NO_NODE)
                {
                    return NO_NODE;
                }
                ctx.nodes[or_index].children.push_back(comparison_index);
            }

            ctx.nodes[case_index].children.push_back(or_index);
        }
        else
        {
            NodeIndex comparison_index = parse_label_comparison(label_json);
            if (comparison_index == NO_NODE)
            {
                return NO_NODE;
            }

            ctx.nodes[case_index].children.push_back(comparison_index);
        }

        // Parse the output
        const auto& output_json = hrz::json::get_nth_or_null(json, (i + 1) * 2 + 1);
        assert(!label_json.IsNull());

        NodeIndex output_index = parse_expression(output_json, expected_type, ctx);
        if (output_index == NO_NODE)
        {
            HRZ_LOG_ERROR("Failed to parse output {} of operator \"match\"", i);
            return NO_NODE;
        }

        ctx.nodes[case_index].children.push_back(output_index);
    }

    const auto& fallback_json = json::get_nth_or_null(json, json.GetArray().Size() - 1);
    assert(!fallback_json.IsNull());

    NodeIndex fallback_index = parse_expression(fallback_json, expected_type, ctx);
    if (fallback_index == NO_NODE)
    {
        HRZ_LOG_ERROR("Failed to parse fallback of operator \"match\"");
        return NO_NODE;
    }

    ctx.nodes[case_index].children.push_back(fallback_index);

    collapse_case_conditions(case_index, ctx);

    return case_index;
}

NodeIndex parse_in_operator(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    // @Todo The support for this operator is only partial.
    // https://docs.mapbox.com/style-spec/reference/expressions/#in
    //
    // According to the Mapbox spec, we should also:
    // - Check for the presence of a substring within a string (requires styling script upgrade)
    // - Accept literal string, boolean and number inputs

    if (expected_type != MapboxPropertyType::Bool && expected_type != MapboxPropertyType::Any)
    {
        HRZ_LOG_ERROR("Unsupported \"in\" operator for something other than a boolean property");
        return NO_NODE;
    }

    auto operand_count = json.GetArray().Size() - 1;
    if (operand_count < 2)
    {
        HRZ_LOG_ERROR(
            "Invalid operand count for operator \"in\", expected at least 2", (int)MapboxOp::In);
        return NO_NODE;
    }

    const auto& keyword_json = hrz::json::get_nth_or_null(json, 1);
    NodeIndex keyword_index = NO_NODE;

    if (keyword_json.IsString())
    {
        // Even though this is stated nowhere in the documentation, the first argument to the "in"
        // operator is interpreted as the name of an attribute... and according to the Mapbox Docs
        // AI, this may depend on the context in which the operator is used, which is ALSO not
        // specified anywhere...
        keyword_index = parse_attribute(keyword_json, ctx);
    }
    else if (keyword_json.IsArray() && !keyword_json.GetArray().Empty())
    {
        // If the input is a string literal wrapped inside a "literal" operator though, it should
        // not be interpreted as the name of an attribute.
        NodeIndex operator_index = parse_expression(keyword_json, MapboxPropertyType::Any, ctx);
        if (operator_index != NO_NODE)
        {
            const auto& node = ctx.nodes.at(operator_index);
            if (node.type != Node::Type::Literal && node.type != Node::Type::Attribute)
            {
                HRZ_LOG_ERROR(
                    "Unsupported keyword type for the \"in\" operator. \"literal\" and \"get\" "
                    "expressions are the only supported expressions for now");
                return NO_NODE;
            }

            keyword_index = operator_index;
        }
        else
        {
            HRZ_LOG_ERROR("Error parsing keyword expression of \"in\" operator");
            return NO_NODE;
        }
    }

    if (keyword_index == NO_NODE)
    {
        HRZ_LOG_ERROR(
            "Unsupported input type for the \"in\" operator. Only attribute inputs provided as a "
            "literal string are supported for now");
        return NO_NODE;
    }

    hrz::InlinedVector<NodeIndex, 4> comparison_indices;
    auto add_comparison = [&](NodeIndex input_index) {
        comparison_indices.push_back(ctx.add_node(Node::Type::Equal, {keyword_index, input_index}));
    };

    for (uint32_t i = 1; i < operand_count; ++i)
    {
        const auto& ref_json = hrz::json::get_nth_or_null(json, i + 1);

        if (ref_json.IsArray())
        {
            NodeIndex index = parse_expression(ref_json, MapboxPropertyType::Any, ctx);
            if (index == NO_NODE)
            {
                HRZ_LOG_ERROR("Failed to parse input expression {} to the \"in\" operator", i - 1);
                continue;
            }

            const auto& ref_node = ctx.nodes.at(index);
            if (ref_node.type == Node::Type::Array)
            {
                if (ref_node.children.empty())
                {
                    HRZ_LOG_ERROR("Unsupported input array to the \"in\" operator: array is empty");
                    continue;
                }

                for (auto child : ref_node.children)
                {
                    add_comparison(child);
                }
            }
            else
            {
                add_comparison(index);
            }
        }
        else
        {
            NodeIndex index = parse_literal(ref_json, MapboxPropertyType::Any, ctx);
            if (index == NO_NODE)
            {
                HRZ_LOG_ERROR("Failed to parse input literal {} to the \"in\" operator", i - 1);
                continue;
            }

            add_comparison(index);
        }
    }

    if (comparison_indices.empty())
    {
        HRZ_LOG_ERROR(
            "Failed to parse \"in\" operator: could not infer any comparison. \"true\" will be "
            "used instead");
        return ctx.add_literal(Value::from(true));
    }
    else if (comparison_indices.size() == 1)
    {
        return comparison_indices.front();
    }
    else
    {
        NodeIndex or_node = ctx.add_node(Node::Type::Or);
        for (auto index : comparison_indices)
        {
            ctx.nodes[or_node].children.push_back(index);
        }

        return or_node;
    }
}

NodeIndex parse_operator(
    const rapidjson::Value& json,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    auto traverse_operator_ =
        [&json, &ctx](MapboxOp op, MapboxPropertyType operator_expected_type) -> NodeIndex
    {
        CHECK_OPERAND_COUNT(op);

        NodeIndex operator_index = ctx.add_node(mapbox_operator_to_node_type(op));

        for (uint8_t i = 1; i < json.GetArray().Size(); ++i)
        {
            NodeIndex operand_index =
                parse_expression(json::get_nth_or_null(json, i), operator_expected_type, ctx);
            if (operand_index == NO_NODE)
            {
                HRZ_LOG_ERROR("Failed to parse operand of operator ({})", (int)op);
                return NO_NODE;
            }

            ctx.nodes[operator_index].children.push_back(operand_index);
        }

        return operator_index;
    };

    auto traverse_operator = [&](MapboxOp op) -> NodeIndex
    { return traverse_operator_(op, expected_type); };

    auto traverse_boolean_operator = [&](MapboxOp op, bool boolean_operands = false) -> NodeIndex
    {
        return traverse_operator_(
            op, boolean_operands ? MapboxPropertyType::Bool : MapboxPropertyType::Any);
    };

    if (json.IsNull()) return NO_NODE;

    if (json.IsArray())
    {
        if (json.GetArray().Size() == 0)
        {
            HRZ_LOG_ERROR("Invalid Mapbox style expression.");
            return NO_NODE;
        }

        auto op_str = json::as_str(json::get_nth_or_null(json, 0));
        if (!op_str.has_value()) return NO_NODE;

        switch (op_str.value()[0])
        {
            case 'a':
            {
                if (strcmp(op_str.value(), "all") == 0)
                {
                    return traverse_boolean_operator(MapboxOp::All, true);
                }
                else if (strcmp(op_str.value(), "any") == 0)
                {
                    return traverse_boolean_operator(MapboxOp::Any, true);
                }
                else if (strcmp(op_str.value(), "abs") == 0)
                {
                    if (expected_type == MapboxPropertyType::Number
                        || expected_type == MapboxPropertyType::Any)
                    {
                        return traverse_operator(MapboxOp::Abs);
                    }
                    else
                    {
                        HRZ_LOG_ERROR(
                            "Got \"abs\" operator while expecting an expression of type {}",
                            (int)expected_type);
                        return NO_NODE;
                    }
                }
            }
            case 'c':
            {
                if (strcmp(op_str.value(), "coalesce") == 0)
                {
                    if (json.GetArray().Size() < 1)
                    {
                        HRZ_LOG_ERROR("No operands for \"coalesce\" operator");
                        return NO_NODE;
                    }

                    // @Todo(994)
                    // `coalesce` should return the first non-null value among its operands.
                    // We can't do that, so we just return the first operand.
                    // https://docs.mapbox.com/style-spec/reference/expressions/#coalesce
                    return parse_expression(json.GetArray()[0], expected_type, ctx);
                }
                else if (strcmp(op_str.value(), "case") == 0)
                {
                    return parse_case_operator(json, expected_type, ctx);
                }
                else if (strcmp(op_str.value(), "ceil") == 0)
                {
                    return traverse_operator(MapboxOp::Ceil);
                }
                break;
            }
            case 'f':
            {
                if (strcmp(op_str.value(), "format") == 0)
                {
                    return parse_format_operator(json, expected_type, ctx);
                }
                else if (strcmp(op_str.value(), "floor") == 0)
                {
                    return traverse_operator(MapboxOp::Floor);
                }
                break;
            }
            case 'g':
            {
                if (strcmp(op_str.value(), "get") == 0)
                {
                    // Mapbox `get` operator used to retrieve attributes inside a dataset.
                    // https://docs.mapbox.com/style-spec/reference/expressions/#get
                    // @Note: The second attribute `object` described in the specification isn't
                    // supported.
                    CHECK_OPERAND_COUNT(MapboxOp::Get);
                    return parse_attribute(json::get_nth_or_null(json, 1), ctx);
                }
                break;
            }
            case 'h':
            {
                if (strcmp(op_str.value(), "heatmap-density") == 0)
                {
                    // "heatmap-density" is a special input for colourising heatmaps.
                    // We generate a special node, that cannot be turned into style script.
                    // The heatmap layer translation code can handle it, but if encountered
                    // anywhere else it triggers an error.
                    if (expected_type == MapboxPropertyType::Number)
                    {
                        return ctx.add_node(Node::Type::HeatmapIntensity);
                    }
                    else
                    {
                        HRZ_LOG_ERROR(
                            "Got \"heatmap-density\" operator while expecting an expression of "
                            "type {}",
                            (int)expected_type);
                        return NO_NODE;
                    }
                }
                break;
            }
            case 'i':
            {
                if (strcmp(op_str.value(), "in") == 0)
                {
                    return parse_in_operator(json, expected_type, ctx);
                }
                else if (strcmp(op_str.value(), "interpolate") == 0)
                {
                    return parse_interpolate_operator(
                        json, expected_type, InterpolationOperator::Interpolate, ctx);
                }
                else if (strcmp(op_str.value(), "interpolate-hcl") == 0)
                {
                    return parse_interpolate_operator(
                        json, expected_type, InterpolationOperator::InterpolateHcl, ctx);
                }
                else if (strcmp(op_str.value(), "interpolate-lab") == 0)
                {
                    return parse_interpolate_operator(
                        json, expected_type, InterpolationOperator::InterpolateLab, ctx);
                }
                else if (strcmp(op_str.value(), "image") == 0)
                {
                    if (json.GetArray().Size() < 1)
                    {
                        HRZ_LOG_ERROR("No operands for \"image\" operator");
                        return NO_NODE;
                    }

                    // The `image` operator is supposed to return a resolved image from a
                    // string representing the name of a sprite present in the sprite index
                    // JSON.
                    // When used in pair with `coalesce`, it returns the first valid image defined
                    // in the sprite index.
                    // https://docs.mapbox.com/style-spec/reference/expressions/#types-image
                    // @TODO: Pass the sprite index from the main parser over here to check for the
                    // existence of the sprite.
                    // @TODO: Add an image type so that this operator cannot be used to return
                    // other kinds of values, even though it's probably not a big deal.
                    return parse_expression(json.GetArray()[0], expected_type, ctx);
                }
                break;
            }
            case 'l':
            {
                if (strcmp(op_str.value(), "literal") == 0)
                {
                    std::optional<MapboxPropertyType> component_type =
                        mapbox_property_type_array_to_component_type(expected_type);

                    if (!component_type.has_value() && expected_type == MapboxPropertyType::Any)
                    {
                        component_type = MapboxPropertyType::Any;
                    }

                    if (!component_type.has_value())
                    {
                        HRZ_LOG_ERROR(
                            "Got \"literal\" operator while expecting an expression of type {}",
                            (int)expected_type);
                        return NO_NODE;
                    }

                    return parse_array_literal(
                        json::get_nth_or_null(json, 1), component_type.value(), ctx);
                }
            }
            case 'm':
            {
                if (strcmp(op_str.value(), "min") == 0)
                {
                    // Mapbox `min` operator.
                    // https://docs.mapbox.com/style-spec/reference/expressions/#min
                    return traverse_operator(MapboxOp::Min);
                }
                else if (strcmp(op_str.value(), "max") == 0)
                {
                    // Mapbox `max` operator.
                    // https://docs.mapbox.com/style-spec/reference/expressions/#max
                    return traverse_operator(MapboxOp::Max);
                }
                else if (strcmp(op_str.value(), "match") == 0)
                {
                    return parse_match_operator(json, expected_type, ctx);
                }
                break;
            }
            case 'r':
            {
                if (strcmp(op_str.value(), "round") == 0)
                {
                    return traverse_operator(MapboxOp::Round);
                }
                break;
            }
            case 's':
            {
                if (strcmp(op_str.value(), "step") == 0)
                {
                    return parse_step_operator(json, expected_type, ctx);
                }
                break;
            }
            case 't':
            {
                if (strcmp(op_str.value(), "to-string") == 0)
                {
                    return parse_to_string_operator(json, expected_type, ctx);
                }
                else if (strcmp(op_str.value(), "typeof") == 0)
                {
                    return parse_typeof_operator(json, expected_type, ctx);
                }
            }
            case '+':
            {
                return traverse_operator(MapboxOp::Add);
            }
            case '-':
            {
                return traverse_operator(MapboxOp::Subtract);
            }
            case '*':
            {
                return traverse_operator(MapboxOp::Multiply);
            }
            case '/':
            {
                return traverse_operator(MapboxOp::Divide);
            }
            case '%':
            {
                return traverse_operator(MapboxOp::Remainder);
            }
            case '!':
            {
                if (strcmp(op_str.value(), "!") == 0)
                {
                    return traverse_boolean_operator(MapboxOp::Not, true);
                }
                else if (strcmp(op_str.value(), "!=") == 0)
                {
                    return traverse_boolean_operator(MapboxOp::NotEqual);
                }
            }
            case '=':
            {
                if (strcmp(op_str.value(), "==") == 0)
                {
                    return traverse_boolean_operator(MapboxOp::Equal);
                }
            }
            case '<':
            {
                if (strcmp(op_str.value(), "<") == 0)
                {
                    return traverse_boolean_operator(MapboxOp::Lesser);
                }
                else if (strcmp(op_str.value(), "<=") == 0)
                {
                    return traverse_boolean_operator(MapboxOp::LesserOrEqual);
                }
            }
            case '>':
            {
                if (strcmp(op_str.value(), ">") == 0)
                {
                    return traverse_boolean_operator(MapboxOp::Greater);
                }
                else if (strcmp(op_str.value(), ">=") == 0)
                {
                    return traverse_boolean_operator(MapboxOp::GreaterOrEqual);
                }
            }
            default:
            {
                HRZ_LOG_WARNING("Unknown or unsupported operator '{}'.", op_str.value());
                break;
            }
        }

        return NO_NODE;
    }
    else
    {
        return parse_literal(json, expected_type, ctx);
    }
}

NodeIndex parse_expression(
    const rapidjson::Value& root,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx)
{
    // https://docs.mapbox.com/style-spec/reference/expressions/

    if (root.IsNull()) return NO_NODE;

    if (root.IsArray())
    {
        return parse_operator(root, expected_type, ctx);
    }
    else
    {
        return parse_literal(root, expected_type, ctx);
    }
}

namespace
{
void finalize_case(NodeIndex index, NodeIndex parent_index, ExpressionContext& ctx)
{
    assert(ctx.nodes.at(index).type == Node::Type::Case);

    if (parent_index != NO_NODE && ctx.nodes.at(parent_index).type != Node::Type::Case)
    {
        // If there is a case node in the expression, we want to make it the expression root to
        // ease the styling script generation.
        swap_case_node_with_parent(index, parent_index, ctx);
    }
}

void finalize_and(NodeIndex index, NodeIndex, ExpressionContext& ctx)
{
    assert(ctx.nodes.at(index).type == Node::Type::And);

    auto& node = ctx.nodes.at(index);

    // The Mapbox "all" operator can be used with any number of operands.
    // When used without operands, it returns true.
    if (node.children.empty())
    {
        node.type = Node::Type::Literal;
        node.literal = Value::from(true);
    }
    else if (node.children.size() == 1)
    {
        const auto& child = ctx.nodes.at(node.children.front());
        node = child;
    }
}

void finalize_or(NodeIndex index, NodeIndex, ExpressionContext& ctx)
{
    assert(ctx.nodes.at(index).type == Node::Type::Or);

    auto& node = ctx.nodes.at(index);

    // "any" can be used with any number of operands.
    // When used without operands, it returns false.
    if (node.children.empty())
    {
        node.type = Node::Type::Literal;
        node.literal = Value::from(false);
    }
    else if (node.children.size() == 1)
    {
        const auto& child = ctx.nodes.at(node.children.front());
        node = child;
    }
}

// Depth first.
void visit_children(
    NodeIndex root,
    ExpressionContext& ctx,
    const std::function<void(NodeIndex index, NodeIndex parent_index)>& callback)
{
    for (auto child : ctx.nodes.at(root).children)
    {
        // Nodes might have been deleted during the visit to the other children
        if (child < ctx.nodes.size())
        {
            visit_children(child, ctx, callback);
            callback(child, root);
        }
    }
}

// Depth first.
void visit_nodes(
    NodeIndex root,
    ExpressionContext& ctx,
    const std::function<void(NodeIndex index, NodeIndex parent_index)>& callback)
{
    visit_children(root, ctx, callback);
    callback(root, NO_NODE);
}
} // namespace

void finalize_expression(NodeIndex root, ExpressionContext& ctx)
{
    if (root == NO_NODE)
    {
        return;
    }

#if DEBUG_PRINT_EXPRESSION_TREES
    HRZ_LOG_DEBUG("Expression tree, pre-finalization:");
    print_node_tree(root, ctx);
#endif

    // We need to finalize case trees before finalizing the rest of the expression, as they
    // can make drastic changes to the expression tree.
    visit_nodes(
        root, ctx,
        [&](NodeIndex index, NodeIndex parent_index)
        {
            if (ctx.nodes.at(index).type == Node::Type::Case)
            {
                finalize_case(index, parent_index, ctx);
            }
        });

    visit_nodes(
        root, ctx,
        [&](NodeIndex index, NodeIndex parent_index)
        {
            const auto& node = ctx.nodes.at(index);

            if (node.type == Node::Type::And)
            {
                finalize_and(index, parent_index, ctx);
            }
            else if (node.type == Node::Type::Or)
            {
                finalize_or(index, parent_index, ctx);
            }
        });

#if DEBUG_PRINT_EXPRESSION_TREES
    HRZ_LOG_DEBUG("Expression tree, post-finalization:");
    print_node_tree(root, ctx);
#endif
}

void generate_sub_expression_script(
    gsl::span<const Node> nodes,
    NodeIndex current,
    std::string& script)
{
#define GENERATE_SUB_EXPR_FOR_CHILD(CHILD)                                                       \
    assert((CHILD) < nodes.size() && node.children.size() > CHILD && node.children[CHILD] != 0); \
    generate_sub_expression_script(nodes, node.children[(CHILD)], script)

#define GENERATE_SINGLE_FUNCTION_CALL(FUNCTION_NAME, OPERAND_COUNT) \
    script += FUNCTION_NAME "(";                                    \
    for (uint32_t i = 0; i < OPERAND_COUNT - 1; ++i)                \
    {                                                               \
        GENERATE_SUB_EXPR_FOR_CHILD(i);                             \
        script += ", ";                                             \
    }                                                               \
    GENERATE_SUB_EXPR_FOR_CHILD(OPERAND_COUNT - 1);                 \
    script += ")";

    // Chains calls to a function that takes two operands until there are no more children left.
    // Examples:
    //  "min(a, min(b, c))"
    //  "max(a, max(b, max(c, d)))"
#define GENERATE_CHAINED_FUNCTION_CALLS(FUNCTION_NAME)      \
    for (uint32_t i = 0; i < node.children.size() - 1; ++i) \
    {                                                       \
        script += FUNCTION_NAME "(";                        \
        GENERATE_SUB_EXPR_FOR_CHILD(i);                     \
        script += ", ";                                     \
    }                                                       \
    GENERATE_SUB_EXPR_FOR_CHILD(node.children.size() - 1);  \
    for (uint32_t i = 0; i < node.children.size() - 1; ++i) \
    {                                                       \
        script += ")";                                      \
    }

#define GENERATE_UNARY_OPERATOR(OPERATOR) \
    script += "(" OPERATOR;               \
    GENERATE_SUB_EXPR_FOR_CHILD(0);       \
    script += ")";

#define GENERATE_BINARY_OPERATOR(OPERATOR) \
    script += "(";                         \
    GENERATE_SUB_EXPR_FOR_CHILD(0);        \
    script += " " OPERATOR " ";            \
    GENERATE_SUB_EXPR_FOR_CHILD(1);        \
    script += ")";

    // Chains binary operators until there are no more children left.
    // Examples:
    //  "a and b and c"
    //  "a or b or c or d or e"
#define GENERATE_CHAINED_BINARY_OPERATOR(OPERATOR)      \
    script += "(";                                      \
    GENERATE_SUB_EXPR_FOR_CHILD(0);                     \
    for (uint32_t i = 1; i < node.children.size(); ++i) \
    {                                                   \
        script += " " OPERATOR " ";                     \
        GENERATE_SUB_EXPR_FOR_CHILD(i);                 \
    }                                                   \
    script += ")";

    assert(current != 0 && current < nodes.size());

    const Node& node = nodes[current];

    switch (node.type)
    {
        case Node::Type::Attribute:
        {
            GENERATE_SINGLE_FUNCTION_CALL("attr", 1);
            break;
        }
        case Node::Type::Format:
        {
            GENERATE_SINGLE_FUNCTION_CALL("fmt", node.children.size());
            break;
        }
        case Node::Type::Min:
        {
            GENERATE_CHAINED_FUNCTION_CALLS("min");
            break;
        }
        case Node::Type::Max:
        {
            GENERATE_CHAINED_FUNCTION_CALLS("max");
            break;
        }
        case Node::Type::Add:
        {
            GENERATE_CHAINED_FUNCTION_CALLS("add");
            break;
        }
        case Node::Type::Subtract:
        {
            if (node.children.size() == 1)
            {
                GENERATE_SINGLE_FUNCTION_CALL("neg", 1);
            }
            else
            {
                GENERATE_SINGLE_FUNCTION_CALL("sub", 2);
            }
            break;
        }
        case Node::Type::Multiply:
        {
            GENERATE_CHAINED_FUNCTION_CALLS("mul");
            break;
        }
        case Node::Type::Divide:
        {
            GENERATE_SINGLE_FUNCTION_CALL("div", 2);
            break;
        }
        case Node::Type::Remainder:
        {
            // @Todo Mapbox uses the remainder of the integer division, whereas we use the modulo,
            // which yields different result.
            // Example:
            //  Mapbox:     ['%', -1, 4]   -> -1
            //  Horizon:    mod(-1, 4)     ->  3
            GENERATE_SINGLE_FUNCTION_CALL("mod", 2);
            break;
        }
        case Node::Type::Abs:
        {
            GENERATE_SINGLE_FUNCTION_CALL("abs", 1);
            break;
        }
        case Node::Type::Round:
        {
            GENERATE_SINGLE_FUNCTION_CALL("round", 1);
            break;
        }
        case Node::Type::Floor:
        {
            GENERATE_SINGLE_FUNCTION_CALL("floor", 1);
            break;
        }
        case Node::Type::Ceil:
        {
            GENERATE_SINGLE_FUNCTION_CALL("ceil", 1);
            break;
        }
        case Node::Type::Typeof:
        {
            GENERATE_SINGLE_FUNCTION_CALL("mapbox_typeof", 1);
            break;
        }
        case Node::Type::ToString:
        {
            GENERATE_SINGLE_FUNCTION_CALL("to_string", 1);
            break;
        }
        case Node::Type::Literal:
        {
            if (node.literal.type == Value::Type::String)
            {
                script += fmt::format("\"{}\"", to_string(node.literal));
            }
            else
            {
                script += to_string(node.literal);
            }
            break;
        }
        case Node::Type::Colorize:
        {
            GENERATE_SINGLE_FUNCTION_CALL("colorize", 2);
            break;
        }
        case Node::Type::Not:
        {
            GENERATE_UNARY_OPERATOR("not");
            break;
        }
        case Node::Type::Equal:
        {
            GENERATE_BINARY_OPERATOR("==");
            break;
        }
        case Node::Type::NotEqual:
        {
            GENERATE_BINARY_OPERATOR("!=");
            break;
        }
        case Node::Type::Lesser:
        {
            GENERATE_BINARY_OPERATOR("<");
            break;
        }
        case Node::Type::LesserOrEqual:
        {
            GENERATE_BINARY_OPERATOR("<=");
            break;
        }
        case Node::Type::Greater:
        {
            GENERATE_BINARY_OPERATOR(">");
            break;
        }
        case Node::Type::GreaterOrEqual:
        {
            GENERATE_BINARY_OPERATOR(">=");
            break;
        }
        case Node::Type::And:
        {
            assert(node.children.size() >= 2);
            GENERATE_CHAINED_BINARY_OPERATOR("and");
            break;
        }
        case Node::Type::Or:
        {
            assert(node.children.size() >= 2);
            GENERATE_CHAINED_BINARY_OPERATOR("or");
            break;
        }
        case Node::Type::HeatmapIntensity:
        {
            HRZ_LOG_ERROR("Unexpected heatmap-intensity node in script generation");
            script += "0.0";
            break;
        }
        case Node::Type::Case:
        {
            HRZ_LOG_WARNING(
                "Encountered \"case\" or \"match\" operators where not supported: their default "
                "output will be used instead.");
            GENERATE_SUB_EXPR_FOR_CHILD(node.children.size() - 1);
            break;
        }
        default:
        {
            assert(false && "Unhandled type");
            break;
        }
    }

#undef GENERATE_CHAINED_FUNCTION_CALLS
#undef GENERATE_SINGLE_FUNCTION_CALL
#undef GENERATE_SUB_EXPR_FOR_CHILD
}

#if HRZ_DEBUG
std::string value_to_string(const Value& value)
{
    switch (value.type)
    {
        case Value::Type::Bool: return fmt::format("{}", value.b64);
        case Value::Type::UInt: return fmt::format("{}", value.u64);
        case Value::Type::String: return fmt::format("\"{}\"", value.str);
        case Value::Type::Double: return fmt::format("{}", value.f64);
        default: assert(false && "Unhandled"); return "???";
    }
}

void print_sub_tree(NodeIndex root, const ExpressionContext& ctx, std::string indentation)
{
    static std::string literal_string;

    const auto& node = ctx.nodes.at(root);
    if (node.type == Node::Type::Literal)
    {
        literal_string = value_to_string(node.literal);
    }
    else if (!literal_string.empty())
    {
        literal_string.clear();
    }

    HRZ_LOG_DEBUG("{} > {} {}", indentation, (int)node.type, literal_string);

    for (auto child : node.children)
    {
        print_sub_tree(child, ctx, indentation + "    ");
    }
}

void print_node_tree(NodeIndex root, const ExpressionContext& ctx)
{
    print_sub_tree(root, ctx, "");
}
#endif

} // namespace hrz_mapbox
