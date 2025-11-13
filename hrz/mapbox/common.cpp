#include "hrz/mapbox/common.h"

#include "hrz/common/color.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/string_utils.h"
#include "hrz/mapbox/expression.h"

#include <fmt/format.h>

namespace hrz_mapbox
{
namespace
{
class Script
{
    std::string* _script = nullptr;
    std::string _indentation = "";

public:
    explicit Script(std::string* script) : _script(script) {}

    void new_line()
    {
        if (!_script->empty())
        {
            *_script += "\n" + _indentation;
        }
    }

    template<typename... T>
    void new_line(fmt::format_string<T...> fmt, T&&... args)
    {
        new_line();
        append(fmt, std::forward<T>(args)...);
    }

    void indent() { _indentation += '\t'; }

    void unindent()
    {
        assert(!_indentation.empty());
        _indentation.pop_back();
    }

    void append(const char* str) { *_script += str; }

    template<typename... T>
    void append(fmt::format_string<T...> fmt, T&&... args)
    {
        *_script += fmt::format(fmt, std::forward<T>(args)...);
    }

    std::string* mutable_string() { return _script; }

    const std::string& indentation() const { return _indentation; }
};

using GenerationFn = std::function<void(std::span<const Node>, NodeIndex node, Script& script)>;

void generate_script_for_node(
    std::span<const Node> nodes,
    NodeIndex root,
    Script& script,
    const GenerationFn& generation);

void generate_script_block(
    std::span<const Node> nodes,
    NodeIndex root,
    Script& script,
    const GenerationFn& generation)
{
    script.new_line("{}", "{");
    script.indent();

    generate_script_for_node(nodes, root, script, generation);

    script.unindent();
    script.new_line("{}", "}");
}

// Returns whether a branch was actually created (if the condition node is a boolean literal,
// we don't create the branch and just generate the code or not, based on the literal value).
bool generate_script_branch(
    std::span<const Node> nodes,
    NodeIndex condition,
    NodeIndex output,
    const char* branch_kind,
    Script& script,
    const GenerationFn& generation)
{
    if (condition && nodes[condition].type == Node::Type::Literal)
    {
        assert(nodes[condition].literal.type == Value::Type::Bool);
        if (nodes[condition].literal.b64)
        {
            generate_script_for_node(nodes, output, script, generation);
        }
        return false;
    }

    if (condition)
    {
        script.new_line("{} (", branch_kind);
        generate_sub_expression_script(nodes, condition, *script.mutable_string());
        script.append(")");
    }
    else
    {
        script.new_line("{}", branch_kind);
    }

    generate_script_block(nodes, output, script, generation);

    return true;
}

void generate_script_for_node(
    std::span<const Node> nodes,
    NodeIndex root,
    Script& script,
    const GenerationFn& generation)
{
    if (nodes[root].type != Node::Type::Case)
    {
        generation(nodes, root, script);
        return;
    }

    auto if_count = nodes[root].children.size() / 2;
    auto branches_created = 0;

    for (uint32_t i = 0; i < if_count; ++i)
    {
        NodeIndex condition = nodes[root].children[i * 2];
        NodeIndex output = nodes[root].children[i * 2 + 1];

        if (generate_script_branch(
                nodes, condition, output, branches_created ? "elif" : "if", script, generation))
        {
            branches_created++;
        }
    }

    if (!branches_created)
    {
        generate_script_for_node(nodes, nodes[root].children.back(), script, generation);
    }
    else
    {
        script.new_line("else");
        generate_script_block(nodes, nodes[root].children.back(), script, generation);
    }
};

void generate_color_expr(
    std::span<const Node> nodes,
    const Value& color_default_value,
    NodeIndex color_node,
    const Value& opacity_default_value,
    NodeIndex opacity_node,
    std::string& color_expr)
{
    std::string opacity_expr;
    bool has_opacity_expr = opacity_node != 0;

    if (has_opacity_expr)
    {
        generate_sub_expression_script(nodes, opacity_node, opacity_expr);
    }

    if (color_node == 0)
    {
        if (has_opacity_expr)
        {
            color_expr = fmt::format("alpha({}, {})", to_string(color_default_value), opacity_expr);
        }
        else
        {
            // Bake the default opacity in the colour.
            Value baked_color = color_default_value;
            baked_color.u64 &=
                (((uint64_t)(opacity_default_value.f64 * 255) << 24) | (uint64_t)0x00ffffff);
            color_expr = to_string(baked_color);
        }
    }
    else
    {
        generate_sub_expression_script(nodes, color_node, color_expr);

        color_expr = has_opacity_expr
            ? fmt::format("alpha({}, {})", color_expr, opacity_expr)
            : fmt::format("alpha({}, {})", color_expr, to_string(opacity_default_value));
    }
}

void generate_internal_property_script(
    std::span<const Node> nodes,
    const Property::Generic& generic,
    std::string_view property_name,
    Script& script)
{
    if (generic.is_literal())
    {
        return;
    }

    generate_script_for_node(
        nodes, generic.node, script,
        [&](std::span<const Node> nodes, NodeIndex node, Script& script)
        {
            script.new_line("set \"{}\" = ", property_name);
            generate_sub_expression_script(nodes, node, *script.mutable_string());
            script.append(";");
        });
}

void generate_internal_property_script(
    std::span<const Node> nodes,
    const Property::Vec2& vec2,
    std::string_view property_name,
    Script& script)
{
    std::string component_property_name = (std::string)property_name;
    component_property_name += "_x";
    generate_internal_property_script(nodes, vec2.x, component_property_name, script);
    component_property_name.back() = 'y';
    generate_internal_property_script(nodes, vec2.y, component_property_name, script);
}

void generate_internal_property_script(
    std::span<const Node> nodes,
    const Property::Vec3& vec3,
    std::string_view property_name,
    Script& script)
{
    std::string component_property_name = (std::string)property_name;
    component_property_name += "_x";
    generate_internal_property_script(nodes, vec3.x, component_property_name, script);
    component_property_name.back() = 'y';
    generate_internal_property_script(nodes, vec3.y, component_property_name, script);
    component_property_name.back() = 'z';
    generate_internal_property_script(nodes, vec3.z, component_property_name, script);
}

void generate_internal_property_script(
    std::span<const Node> nodes,
    const Property::ColorWithOpacity& color_with_opacity,
    std::string_view property_name,
    Script& script)
{
    if (color_with_opacity.is_literal())
    {
        return;
    }

    generate_script_for_node(
        nodes, color_with_opacity.color.node, script,
        [&](std::span<const Node> nodes, NodeIndex color_index, Script& script)
        {
            generate_script_for_node(
                nodes, color_with_opacity.opacity.node, script,
                [&](std::span<const Node> nodes, NodeIndex opacity_index, Script& script)
                {
                    std::string color_expr;
                    generate_color_expr(
                        nodes, color_with_opacity.color.default_value, color_index,
                        color_with_opacity.opacity.default_value, opacity_index, color_expr);

                    script.new_line("set \"{}\" = {};", property_name, color_expr);
                });
        });
}

void generate_internal_property_script(
    std::span<const Node> nodes,
    const Property::ExtrudedVectorColor& extruded_color,
    std::string_view property_name,
    Script& script)
{
    // We have cases where everything is literal so we could theoretically encode everything in
    // default values. Though, when a gradient on facades is asked we don't want to manually compute
    // darken() and lighten() stuff so we rely on the styling script for this.
    if (extruded_color.is_never_gradient() && extruded_color.color.is_literal()
        && extruded_color.opacity.is_literal())
    {
        return;
    }

    generate_script_for_node(
        nodes, extruded_color.color.node, script,
        [&](std::span<const Node>, NodeIndex color_index, Script& script)
        {
            generate_script_for_node(
                nodes, extruded_color.opacity.node, script,
                [&](std::span<const Node>, NodeIndex opacity_index, Script& script)
                {
                    generate_script_for_node(
                        nodes, extruded_color.gradient.node, script,
                        [&](std::span<const Node>, NodeIndex gradient_index, Script& script)
                        {
                            std::string color_expr;
                            generate_color_expr(
                                nodes, extruded_color.color.default_value, color_index,
                                extruded_color.opacity.default_value, opacity_index, color_expr);

                            // This is the most complex case because we add a branching condition to
                            // check the result of the vertical-gradient expression before choosing
                            // if we need to generate the gradient.
                            if (!extruded_color.is_never_gradient())
                            {
                                std::optional<bool> gradient_literal;
                                if (extruded_color.gradient.is_literal())
                                {
                                    gradient_literal = extruded_color.gradient.default_value.b64;
                                }
                                else if (
                                    gradient_index > 0
                                    && nodes[gradient_index].type == Node::Type::Literal
                                    && nodes[gradient_index].literal.type == Value::Type::Bool)
                                {
                                    gradient_literal = nodes[gradient_index].literal.b64;
                                }

                                std::string roof_name;
                                std::string upper_name;
                                std::string lower_name;

                                Property::ExtrudedVectorColor::generate_separate_styling_names(
                                    property_name, &roof_name, &upper_name, &lower_name);

                                if (gradient_literal.has_value() && gradient_literal.value())
                                {
                                    script.new_line(
                                        "set \"{0}\" = lighten({3}, 0.2);\n{4}"
                                        "set \"{1}\" = {3};\n{4}"
                                        "set \"{2}\" = darken({3}, 0.2);",
                                        roof_name, upper_name, lower_name, color_expr,
                                        script.indentation());
                                }
                                else if (gradient_literal.has_value())
                                {
                                    script.new_line(
                                        "set \"{0}\" = {3};\n{4}"
                                        "set \"{1}\" = {3};\n{4}"
                                        "set \"{2}\" = {3};",
                                        roof_name, upper_name, lower_name, color_expr,
                                        script.indentation());
                                }
                                else
                                {
                                    // Applying the gradient is based on an expression.
                                    std::string gradient_expr;
                                    generate_sub_expression_script(
                                        nodes, gradient_index, gradient_expr);

                                    script.new_line(
                                        "if ({0})\n{5}{{\n{5}\t"
                                        "set \"{1}\" = lighten({4}, 0.2);\n{5}\t"
                                        "set \"{2}\" = {4};\n{5}\t"
                                        "set \"{3}\" = darken({4}, 0.2);\n{5}"
                                        "}}\n{5}"
                                        "else\n{5}{{\n{5}\t"
                                        "set \"{1}\" = {4};\n{5}\t"
                                        "set \"{2}\" = {4};\n{5}\t"
                                        "set \"{3}\" = {4};\n{5}"
                                        "}}",
                                        gradient_expr, roof_name, upper_name, lower_name,
                                        color_expr, script.indentation());
                                }
                            }
                            else
                            {
                                script.new_line("set \"{}\" = {};", property_name, color_expr);
                            }
                        });
                });
        });
}

void generate_internal_property_script(
    std::span<const Node> nodes,
    const Property::SymbolAnchorAlignment& anchor_alignment,
    std::string_view property_name,
    Script& script)
{
    if (anchor_alignment.is_literal())
    {
        return;
    }

    generate_script_for_node(
        nodes, anchor_alignment.anchor.node, script,
        [&](std::span<const Node> nodes, NodeIndex node, Script& script)
        {
            // If the node is a literal, then there is no need to wait for styling to check its
            // value. We can do it now, so we just have to set the correct alignment during styling.
            if (nodes[node].type == Node::Type::Literal
                && nodes[node].literal.type == Value::Type::String)
            {
                auto alignment =
                    Property::SymbolAnchorAlignment::mapbox_anchor_to_horizon_alignment(
                        nodes[node].literal.str);
                script.new_line("set \"{}_x\" = {};", property_name, alignment.x);
                script.new_line("set \"{}_y\" = {};", property_name, alignment.y);
                return;
            }

            std::string anchor_expr;
            generate_sub_expression_script(nodes, node, anchor_expr);

            script.new_line(
                "if (({0}) == \"left\") {{\n{2}\t"
                "set \"{1}_x\" = -1;\n{2}"
                "set \"{1}_y\" = 0;\n{2}"
                "}}\n{2}"
                "elif (({0}) == \"right\") {{\n{2}\t"
                "set \"{1}_x\" = 1;\n{2}"
                "set \"{1}_y\" = 0;\n{2}"
                "}}\n{2}"
                "elif (({0}) == \"top\") {{\n{2}\t"
                "set \"{1}_x\" = 0;\n{2}"
                "set \"{1}_y\" = -1;\n{2}"
                "}}\n{2}"
                "elif (({0}) == \"bottom\") {{\n{2}\t"
                "set \"{1}_x\" = 0;\n{2}"
                "set \"{1}_y\" = 1;\n{2}"
                "}}\n{2}"
                "elif (({0}) == \"top-left\") {{\n{2}\t"
                "set \"{1}_x\" = -1;\n{2}"
                "set \"{1}_y\" = -1;\n{2}"
                "}}\n{2}"
                "elif (({0}) == \"top-right\") {{\n{2}\t"
                "set \"{1}_x\" = 1;\n{2}"
                "set \"{1}_y\" = -1;\n{2}"
                "}}\n{2}"
                "elif (({0}) == \"bottom-left\") {{\n{2}\t"
                "set \"{1}_x\" = -1;\n{2}"
                "set \"{1}_y\" = 1;\n{2}"
                "}}\n{2}"
                "elif (({0}) == \"bottom-right\") {{\n{2}\t"
                "set \"{1}_x\" = 1;\n{2}"
                "set \"{1}_y\" = 1;\n{2}"
                "}}\n{2}"
                "else {{\n{2}\t"
                "set \"{1}_x\" = 0;\n{2}"
                "set \"{1}_y\" = 0;\n{2}"
                "}}",
                anchor_expr.c_str(), property_name, script.indentation());
        });
}

void generate_internal_property_script(
    std::span<const Node> nodes,
    const Property::TextAlignment& text_alignment,
    std::string_view property_name,
    Script& script)
{
    if (text_alignment.is_literal())
    {
        return;
    }

    generate_script_for_node(
        nodes, text_alignment.text_justify.node, script,
        [&](std::span<const Node>, NodeIndex justify_index, Script& script)
        {
            generate_script_for_node(
                nodes, text_alignment.text_anchor.node, script,
                [&](std::span<const Node>, NodeIndex anchor_index, Script& script)
                {
                    std::string alignment_from_anchor_expr;
                    if (text_alignment.text_anchor.is_literal())
                    {
                        auto alignment =
                            Property::TextAlignment::mapbox_text_justify_to_horizon_alignment(
                                text_alignment.text_anchor.default_value.str);

                        alignment_from_anchor_expr = fmt::format(
                            "set \"{1}\" = enum(\"TextAlignment\", \"{0}\");\n{2}",
                            hrz_proto::TextAlignment_Name(alignment), property_name,
                            script.indentation());
                    }
                    else
                    {
                        std::string anchor_expr;
                        generate_sub_expression_script(nodes, anchor_index, anchor_expr);

                        alignment_from_anchor_expr = fmt::format(
                            "if (({0}) == \"left\" or ({0}) == \"top-left\" or ({0}) == "
                            "\"bottom-left\") {{\n{2}"
                            "set \"{1}\" = enum(\"TextAlignment\", \"LEFT_ALIGNED\");\n{2}"
                            "}}\n{2}"
                            "elif (({0}) == \"center\" or ({0}) == \"top\" or ({0}) == \"bottom\") "
                            "{{\n{2}"
                            "set \"{1}\" = enum(\"TextAlignment\", \"CENTERED\");\n{2}"
                            "}}\n{2}"
                            "elif (({0}) == \"right\" or ({0}) == \"top-right\" or ({0}) == "
                            "\"bottom-right\") {{\n{2}"
                            "set \"{1}\" = enum(\"TextAlignment\", \"RIGHT_ALIGNED\");\n{2}"
                            "}}\n{2}",
                            anchor_expr, property_name, script.indentation());
                    }

                    if (text_alignment.text_justify.is_literal()
                        && text_alignment.text_justify.default_value.str == "auto")
                    {
                        script.new_line("{}", alignment_from_anchor_expr);
                    }
                    else
                    {
                        std::string justify_expr;
                        generate_sub_expression_script(nodes, justify_index, justify_expr);

                        script.new_line(
                            "if (({0}) == \"auto\") {{\n{3}"
                            "{1}"
                            "}}\n{3}"
                            "elif (({0}) == \"left\") {{\n{3}"
                            "set \"{2}\" = enum(\"TextAlignment\", \"LEFT_ALIGNED\");\n{3}"
                            "}}\n{3}"
                            "elif (({0}) == \"center\") {{\n{3}"
                            "set \"{2}\" = enum(\"TextAlignment\", \"CENTERED\");\n{3}"
                            "}}\n{3}"
                            "elif (({0}) == \"right\") {{\n{3}"
                            "set \"{2}\" = enum(\"TextAlignment\", \"RIGHT_ALIGNED\");\n{3}"
                            "}}\n{3}",
                            justify_expr, alignment_from_anchor_expr, property_name,
                            script.indentation());
                    }
                });
        });
}

void generate_property_script(std::span<const Node> nodes, const Property& prp, Script& script)
{
    switch (prp.type)
    {
        case Property::Type::Generic:
            generate_internal_property_script(nodes, prp.generic, prp.styling_name, script);
            break;

        case Property::Type::Vec2:
            generate_internal_property_script(nodes, prp.vec2, prp.styling_name, script);
            break;

        case Property::Type::Vec3:
            generate_internal_property_script(nodes, prp.vec3, prp.styling_name, script);
            break;

        case Property::Type::ColorWithOpacity:
            generate_internal_property_script(
                nodes, prp.color_with_opacity, prp.styling_name, script);
            break;

        case Property::Type::ExtrudedVectorColor:
            generate_internal_property_script(nodes, prp.extruded_color, prp.styling_name, script);
            break;

        case Property::Type::SymbolAnchorAlignment:
            generate_internal_property_script(
                nodes, prp.symbol_anchor_alignment, prp.styling_name, script);
            break;

        case Property::Type::TextAlignment:
            generate_internal_property_script(nodes, prp.text_alignment, prp.styling_name, script);
            break;

        case Property::Type::Invalid:
        default: assert(false && "Unhandled"); return;
    }
}
} // namespace

std::string to_string(Value value)
{
    switch (value.type)
    {
        case Value::Type::Bool: return value.b64 ? "true" : "false";
        // UInt values are used for colors, so we format them in hexadecimal
        case Value::Type::UInt: return fmt::format("{:#x}", value.u64);
        // Note: we prefer using fmt rather than std::to_string for the nicer formatting
        // (123.0 will result in 123 for fmt, or 123.000000 for std::to_string).
        case Value::Type::Double:
            if (std::floor(value.f64) == value.f64)
            {
                // If the value has no decimal part, we still want to write
                // ".0" so that it doesn't get interpreted as an integer
                return fmt::format("{:.1f}", value.f64);
            }
            else
            {
                return fmt::format("{}", value.f64);
            }
        case Value::Type::String: return std::string(value.str.data(), value.str.size());
        default:
        {
            assert(false && "Unhandled case");
            break;
        }
    }

    return "";
}

void generate_representations_script(
    std::span<const Node> nodes,
    std::span<const Property> properties,
    std::span<const std::string_view> representation_names,
    uint32_t first_representation_id,
    NodeIndex filter_node,
    std::string& string)
{
    if (representation_names.empty())
    {
        assert(false);
        return;
    }

    if (!string.empty() && string.back() != '\n')
    {
        string += "\n";
    }

    Script script(&string);
    for (const auto& representation : representation_names)
    {
        script.new_line("// \"{}\"", representation);
    }

    auto generate_properties_script = [&]()
    {
        for (const auto& property : properties)
        {
            if (property.type == Property::Type::Invalid)
            {
                assert(false);
                continue;
            }

            generate_property_script(nodes, property, script);
        }

        for (size_t i = 0; i < representation_names.size(); ++i)
        {
            script.new_line("fork {{ emit {}; }}", first_representation_id + i);
        }
    };

    if (filter_node != 0 && nodes[filter_node].type != Node::Type::Empty)
    {
        generate_script_for_node(
            nodes, filter_node, script,
            [&](std::span<const Node> nodes, NodeIndex node, Script& script)
            {
                generate_script_branch(
                    nodes, node, 0, "if", script,
                    [&](std::span<const Node>, NodeIndex, Script&)
                    { generate_properties_script(); });
            });
    }
    else
    {
        generate_properties_script();
    }
}

lm::dvec2 Property::SymbolAnchorAlignment::mapbox_anchor_to_horizon_alignment(
    std::string_view anchor)
{
    if (anchor == "left")
    {
        return {-1, 0};
    }
    else if (anchor == "right")
    {
        return {1, 0};
    }
    else if (anchor == "top")
    {
        return {0, -1};
    }
    else if (anchor == "bottom")
    {
        return {0, 1};
    }
    else if (anchor == "top-left")
    {
        return {-1, -1};
    }
    else if (anchor == "top-right")
    {
        return {1, -1};
    }
    else if (anchor == "bottom-left")
    {
        return {-1, 1};
    }
    else if (anchor == "bottom-right")
    {
        return {1, 1};
    }
    else
    {
        return {0, 0};
    }
}

hrz_proto::TextAlignment Property::TextAlignment::mapbox_text_justify_to_horizon_alignment(
    std::string_view text_justify)
{
    if (text_justify == "left")
    {
        return hrz_proto::TextAlignment::LEFT_ALIGNED;
    }
    else if (text_justify == "center")
    {
        return hrz_proto::TextAlignment::CENTERED;
    }
    else if (text_justify == "right")
    {
        return hrz_proto::TextAlignment::RIGHT_ALIGNED;
    }
    else if (text_justify == "auto")
    {
        HRZ_LOG_ERROR("text-justify: \"auto\" should have been handled before!");
        return hrz_proto::TextAlignment::CENTERED;
    }
    else
    {
        return hrz_proto::TextAlignment::CENTERED;
    }
}

void assign_default_value(const Property::Generic& generic, hrz_proto::FloatProperty* proto)
{
    assert(generic.default_value.type == Value::Type::Double);
    proto->set_default_value(generic.default_value.f64);
}

void assign_default_value(const Property::Generic& generic, hrz_proto::IntProperty* proto)
{
    if (generic.default_value.type == Value::Type::Double)
    {
        proto->set_default_value(generic.default_value.f64);
    }
    else if (generic.default_value.type == Value::Type::UInt)
    {
        proto->set_default_value(generic.default_value.u64);
    }
    else
    {
        assert(false && "Unhandled case");
    }
}

void assign_default_value(const Property::Generic& generic, hrz_proto::StringProperty* proto)
{
    assert(generic.default_value.type == Value::Type::String);
    proto->set_default_value(generic.default_value.str.data(), generic.default_value.str.size());
}

void assign_default_value(const Property::Vec2& vec2, hrz_proto::Vec2fProperty* proto)
{
    assert(
        vec2.x.default_value.type == Value::Type::Double
        && vec2.y.default_value.type == Value::Type::Double);
    proto->mutable_default_value()->set_x(vec2.x.default_value.f64);
    proto->mutable_default_value()->set_y(vec2.y.default_value.f64);
}

void assign_default_value(
    const Property::SymbolAnchorAlignment& symbol_anchor_alignment,
    hrz_proto::Vec2fProperty* proto)
{
    assert(symbol_anchor_alignment.anchor.default_value.type == Value::Type::String);
    lm::dvec2 alignment = Property::SymbolAnchorAlignment::mapbox_anchor_to_horizon_alignment(
        symbol_anchor_alignment.anchor.default_value.str);
    proto->mutable_default_value()->set_x(alignment.x);
    proto->mutable_default_value()->set_y(alignment.y);
}

void assign_default_value(const Property::Vec2& vec2, hrz_proto::Vec3fProperty* proto)
{
    assert(
        vec2.x.default_value.type == Value::Type::Double
        && vec2.y.default_value.type == Value::Type::Double);
    proto->mutable_default_value()->set_x(vec2.x.default_value.f64);
    proto->mutable_default_value()->set_y(vec2.y.default_value.f64);
    proto->mutable_default_value()->set_z(0.0f);
}

void assign_default_value(const Property::Vec3& vec3, hrz_proto::Vec3fProperty* proto)
{
    assert(
        vec3.x.default_value.type == Value::Type::Double
        && vec3.y.default_value.type == Value::Type::Double
        && vec3.z.default_value.type == Value::Type::Double);
    proto->mutable_default_value()->set_x(vec3.x.default_value.f64);
    proto->mutable_default_value()->set_y(vec3.y.default_value.f64);
    proto->mutable_default_value()->set_z(vec3.z.default_value.f64);
}

void assign_default_value(const Property::Generic& generic, hrz_proto::ColorProperty* proto)
{
    assert(generic.default_value.type == Value::Type::UInt);
    hrz_proto::Color color_proto = hrz::convert_uint_to_proto_color(generic.default_value.u64);
    proto->mutable_default_value()->CopyFrom(color_proto);
}

void assign_default_value(
    const Property::ColorWithOpacity& color_with_opacity,
    hrz_proto::ColorProperty* proto)
{
    assert(
        color_with_opacity.color.default_value.type == Value::Type::UInt
        && color_with_opacity.opacity.default_value.type == Value::Type::Double);
    hrz_proto::Color color_proto =
        hrz::convert_uint_to_proto_color(color_with_opacity.color.default_value.u64);
    color_proto.set_a(color_with_opacity.opacity.default_value.f64);
    proto->mutable_default_value()->CopyFrom(color_proto);
}

void assign_default_value(
    const Property::TextAlignment& text_alignment,
    hrz_proto::TextAlignmentProperty* proto)
{
    assert(
        text_alignment.text_justify.default_value.type == Value::Type::String
        && text_alignment.text_anchor.default_value.type == Value::Type::String);

    hrz_proto::TextAlignment aligment_proto;
    if (text_alignment.text_justify.default_value.str == "auto")
    {
        lm::dvec2 alignment = Property::SymbolAnchorAlignment::mapbox_anchor_to_horizon_alignment(
            text_alignment.text_anchor.default_value.str);
        if (alignment.x < 0.0)
        {
            aligment_proto = hrz_proto::TextAlignment::LEFT_ALIGNED;
        }
        else if (alignment.x > 0.0)
        {
            aligment_proto = hrz_proto::TextAlignment::RIGHT_ALIGNED;
        }
        else
        {
            aligment_proto = hrz_proto::TextAlignment::CENTERED;
        }
    }
    else
    {
        aligment_proto = Property::TextAlignment::mapbox_text_justify_to_horizon_alignment(
            text_alignment.text_justify.default_value.str);
    }

    proto->set_default_value(aligment_proto);
}
} // namespace hrz_mapbox
