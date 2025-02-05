#include <hrz_fnd_defines.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_json_utils.h>
#include <hrz_mapbox_expression.h>

#include <gtest/gtest.h>
#include <rapidjson/document.h>

#include <string_view>

using namespace hrz_mapbox;

struct Result
{
    bool success;
    std::string_view error_msg;
};

#define EXPECT_RESULT_OK(RESULT)                 \
    auto HRZ_CONCAT(result_, __LINE__) = RESULT; \
    EXPECT_TRUE(HRZ_CONCAT(result_, __LINE__).success) << HRZ_CONCAT(result_, __LINE__).error_msg;

#define EXPECT_RESULT_NOK(RESULT)                \
    auto HRZ_CONCAT(result_, __LINE__) = RESULT; \
    EXPECT_FALSE(HRZ_CONCAT(result_, __LINE__).success) << "Unexpected success";

struct ParseContext
{
    hrz_mapbox::ExpressionContext expr;
    hrz_mapbox::VectorSource::AttributeMap attributes;

    ParseContext() : expr("", &attributes) {}

    void clear()
    {
        attributes.clear();
        expr = hrz_mapbox::ExpressionContext("", &attributes);
    }

    Result parse_expression(
        const char* json,
        MapboxPropertyType expected_type,
        NodeIndex* root_node)
    {
        rapidjson::Document root;
        root.Parse(json);

        if (root.HasParseError() || !root.IsArray())
        {
            return {false, "Invalid test case"};
        }

        NodeIndex first_added_node = (NodeIndex)expr.nodes.size();

        NodeIndex index =
            hrz_mapbox::parse_expression(hrz::json::get_nth_or_null(root, 0), expected_type, expr);
        if (index == NO_NODE)
        {
            return {false, "Failed to parse expression with expected property type"};
        }

        *root_node = index;

        finalize_expression(index, expr);

        // Nodes only keep views to the strings contained in the JSON object, which will go out
        // of scope at the end of the function.
        // So we have to save to keep the strings in this structure.
        for (NodeIndex i = first_added_node; i < expr.nodes.size(); ++i)
        {
            if (expr.nodes[i].type == Node::Type::Literal
                && expr.nodes[i].literal.type == Value::Type::String)
            {
                expr.string_data.push_front(std::string(expr.nodes[i].literal.str));
                expr.nodes[i].literal.str = std::string_view(expr.string_data.front());
            }
        }

        return {true};
    }

    const Node& node(NodeIndex index) { return expr.nodes.at(index); }

    template<typename... Args>
    const Node& node(NodeIndex index, Args... args)
    {
        return get_child(expr.nodes.at(index), args...);
    }

    template<typename... Args>
    const Node& get_child(const Node& node, int child_index, Args... args)
    {
        return get_child(expr.nodes.at(node.children.at(child_index)), args...);
    }

    const Node& get_child(const Node& node) { return node; }
};

#define EXPECT_LITERAL_NODE(NODE, VALUE)                                                \
    EXPECT_EQ(NODE.type, Node::Type::Literal);                                          \
    EXPECT_EQ(NODE.literal.type, VALUE.type);                                           \
    switch (NODE.literal.type)                                                          \
    {                                                                                   \
        case Value::Type::Bool: EXPECT_EQ(NODE.literal.b64, VALUE.b64); break;          \
        case Value::Type::String:                                                       \
            EXPECT_EQ((std::string)NODE.literal.str, (std::string)VALUE.str);           \
            break;                                                                      \
        case Value::Type::Double: EXPECT_DOUBLE_EQ(NODE.literal.f64, VALUE.f64); break; \
        case Value::Type::UInt: EXPECT_EQ(NODE.literal.u64, VALUE.u64); break;          \
        default: FAIL() << "Unhandled Value type";                                      \
    }

#define EXPECT_OPERATOR_NODE(NODE, TYPE, CHILD_COUNT) \
    EXPECT_EQ(NODE.type, TYPE);                       \
    EXPECT_EQ(NODE.children.size(), CHILD_COUNT);

#define EXPECT_ATTRIBUTE_NODE(CTX, NODE, NAME)            \
    EXPECT_OPERATOR_NODE(NODE, Node::Type::Attribute, 1); \
    EXPECT_LITERAL_NODE(CTX.expr.nodes.at(NODE.children.front()), Value::from(NAME));

TEST(MapboxExpression, parse_literals)
{
    ParseContext ctx;
    NodeIndex root_node = 0;

    // Expressions are enclosed in brackets only to be able to create a JSON document and fetch the
    // first item in the list.
    EXPECT_RESULT_OK(ctx.parse_expression("[42]", MapboxPropertyType::Number, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from(42.0));

    EXPECT_RESULT_OK(ctx.parse_expression("[-42]", MapboxPropertyType::Number, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from(-42.0));

    EXPECT_RESULT_OK(ctx.parse_expression("[3.1415]", MapboxPropertyType::Number, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from(3.1415));

    EXPECT_RESULT_OK(ctx.parse_expression("[true]", MapboxPropertyType::Bool, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from(true));

    EXPECT_RESULT_OK(
        ctx.parse_expression("[\"some string\"]", MapboxPropertyType::String, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from("some string"));

    EXPECT_RESULT_OK(ctx.parse_expression("[\"#ff00ff\"]", MapboxPropertyType::String, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from("#ff00ff"));

    EXPECT_RESULT_OK(ctx.parse_expression("[\"#ff00ff\"]", MapboxPropertyType::Color, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from((uint64_t)0xffff00ff));
}

TEST(MapboxExpression, parse_arithmetic_operators)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"+\", 1, 2]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Add, 2);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(1.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(2.0));

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"+\", 1, 2, 3, 4]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Add, 4);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(1.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(2.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 2), Value::from(3.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 3), Value::from(4.0));

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"-\", 1, 2]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Subtract, 2);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(1.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(2.0));

    EXPECT_RESULT_OK(ctx.parse_expression("[[\"-\", 1]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Subtract, 1);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(1.0));

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"*\", 1, 2]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Multiply, 2);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(1.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(2.0));

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"*\", 1, 2, 3]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Multiply, 3);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(1.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(2.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 2), Value::from(3.0));

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"/\", 1, 2]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Divide, 2);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(1.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(2.0));

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"%\", 1, 2]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Remainder, 2);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(1.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(2.0));

    // @Todo: Test for illegal operations, and ensure that whatever works in Mapbox doesn't trigger
    // a parsing error in Horizon (at the very least)

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"abs\", 17]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Abs, 1);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(17.0));

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"abs\", [\"get\", \"attr\"]]]", MapboxPropertyType::Number, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Abs, 1);
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0), "attr");

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"abs\", [\"get\", \"attr\"]]]", MapboxPropertyType::Any, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Abs, 1);
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0), "attr");

    ctx.clear();
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"abs\", [\"get\", \"attr\"]]]", MapboxPropertyType::String, &root_node));

    ctx.clear();
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"abs\", [\"get\", \"attr\"]]]", MapboxPropertyType::Bool, &root_node));
}

TEST(MapboxExpression, parse_array_literal)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"literal\", []]]", MapboxPropertyType::NumberArray, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Array, 0);

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"literal\", [1, 2, 3]]]", MapboxPropertyType::NumberArray, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Array, 3);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(1.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(2.0));
    EXPECT_LITERAL_NODE(ctx.node(root_node, 2), Value::from(3.0));

    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"literal\", [[\"min\", 5, 4], 2]]]", MapboxPropertyType::NumberArray, &root_node));
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"literal\", [[\"get\", \"i-hope-its-a-float-attribute-attribute\"]]]]",
        MapboxPropertyType::NumberArray, &root_node));
    EXPECT_RESULT_NOK(ctx.parse_expression("[[]]", MapboxPropertyType::NumberArray, &root_node));
    EXPECT_RESULT_NOK(
        ctx.parse_expression("[[1, 2, 3]]", MapboxPropertyType::NumberArray, &root_node));
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"literal\", false]]", MapboxPropertyType::NumberArray, &root_node));
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"literal\", [true, false]]]", MapboxPropertyType::NumberArray, &root_node));
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"literal\", [2, false]]]", MapboxPropertyType::NumberArray, &root_node));
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"literal\", [\"hello\"]]]", MapboxPropertyType::NumberArray, &root_node));
}

TEST(MapboxExpression, parse_all_operator)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"all\", [\"get\", \"a\"], [\"get\", \"b\"], [\"get\", \"c\"]]]",
        MapboxPropertyType::Bool, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::And, 3);
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0), "a");
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 1), "b");
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 2), "c");
}

TEST(MapboxExpression, parse_all_operator_single_operand)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"all\", [\"get\", \"a\"]]]", MapboxPropertyType::Bool, &root_node));
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node), "a");
}

TEST(MapboxExpression, parse_all_operator_no_operand)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(ctx.parse_expression("[[\"all\"]]", MapboxPropertyType::Bool, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from(true));
}

TEST(MapboxExpression, parse_any_operator)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"any\", [\"get\", \"a\"], [\"get\", \"b\"], [\"get\", \"c\"]]]",
        MapboxPropertyType::Bool, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Or, 3);
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0), "a");
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 1), "b");
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 2), "c");
}

TEST(MapboxExpression, parse_any_operator_single_operand)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"any\", [\"get\", \"a\"]]]", MapboxPropertyType::Bool, &root_node));
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node), "a");
}

TEST(MapboxExpression, parse_any_operator_no_operand)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(ctx.parse_expression("[[\"any\"]]", MapboxPropertyType::Bool, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from(false));
}

TEST(MapboxExpression, parse_format_operator)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"format\", [\"get\", \"attr\"], {}]]", MapboxPropertyType::FormattedString,
        &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Format, 2);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from("{}"));
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 1), "attr");

    // The last "options" operand is optional
    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"format\", \"text: \", {}, [\"get\", \"attr\"]]]", MapboxPropertyType::FormattedString,
        &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Format, 2);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from("text: {}"));
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 1), "attr");

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"format\", \"{} {} {{} {\", {}, [\"get\", \"attr\"], {}, \"}\"]]",
        MapboxPropertyType::FormattedString, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Format, 2);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from("{{}} {{}} {{{{}} {{{}}}"));
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 1), "attr");

    // Literal values should be collapsed into the format string because we don't literal
    // arguments for the "fmt" function
    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"format\", \"hello\", {}, \"world\", {},  [\"to-string\", 123], {}, [\"get\", "
        "\"attr\"]]]",
        MapboxPropertyType::FormattedString, &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Format, 3);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from("helloworld{}{}"));
    EXPECT_OPERATOR_NODE(ctx.node(root_node, 1), Node::Type::ToString, 1);
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 2), "attr");

    // Problematic but legal case: use of "to-string" on an attribute, so we can't deduce the type
    // of the attribute.
    ctx.attributes.clear();
    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"format\", [\"to-string\", [\"get\", \"attr\"]]]]", MapboxPropertyType::FormattedString,
        &root_node));
    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Format, 2);
    EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from("{}"));
    EXPECT_OPERATOR_NODE(ctx.node(root_node, 1), Node::Type::ToString, 1);

    // If the problematic attribute already has an inferred type, then it should be used.
    ctx.attributes.clear();
    hrz_mapbox::VectorSource::Attribute attr;
    ctx.attributes.insert({"attr", attr});

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"format\", [\"to-string\", [\"get\", \"attr\"]]]]", MapboxPropertyType::FormattedString,
        &root_node));

    // Format strings should not be able to substitute any regular string
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"format\", [\"get\", \"attr\"]]]", MapboxPropertyType::String, &root_node));
    EXPECT_RESULT_NOK(
        ctx.parse_expression("[[\"format\"]]", MapboxPropertyType::FormattedString, &root_node));
}

TEST(MapboxExpression, parse_case_operator)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(
        ctx.parse_expression("[[\"case\", true, 1, 0]]", MapboxPropertyType::Number, &root_node));

    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Case, 3);
    {
        EXPECT_LITERAL_NODE(ctx.node(root_node, 0), Value::from(true));
        EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(1.0));
        EXPECT_LITERAL_NODE(ctx.node(root_node, 2), Value::from(0.0));
    }

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"case\", [\"==\", [\"get\", \"attr\"], 1], 1, 0]]", MapboxPropertyType::Number,
        &root_node));

    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Case, 3);
    {
        EXPECT_OPERATOR_NODE(ctx.node(root_node, 0), Node::Type::Equal, 2);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0, 0), "attr");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 0, 1), Value::from(1.0));
        }
        EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(1.0));

        EXPECT_LITERAL_NODE(ctx.node(root_node, 2), Value::from(0.0));
    }

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"case\", [\"==\", [\"get\", \"attr\"], 0], 0, [\">\", [\"get\", \"attr\"], 1], 1, 2]]",
        MapboxPropertyType::Number, &root_node));

    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Case, 5);
    {
        EXPECT_OPERATOR_NODE(ctx.node(root_node, 0), Node::Type::Equal, 2);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0, 0), "attr");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 0, 1), Value::from(0.0));
        }
        EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(0.0));

        EXPECT_OPERATOR_NODE(ctx.node(root_node, 2), Node::Type::Greater, 2);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 2, 0), "attr");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 2, 1), Value::from(1.0));
        }
        EXPECT_LITERAL_NODE(ctx.node(root_node, 3), Value::from(1.0));

        EXPECT_LITERAL_NODE(ctx.node(root_node, 4), Value::from(2.0));
    }

    EXPECT_RESULT_NOK(ctx.parse_expression("[[\"case\"]]", MapboxPropertyType::Number, &root_node));
    EXPECT_RESULT_NOK(
        ctx.parse_expression("[[\"case\", true, 1]]", MapboxPropertyType::Number, &root_node));
}

TEST(MapboxExpression, parse_match_operator)
{
    ParseContext ctx;
    NodeIndex root_node;

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"match\", [\"get\", \"attr\"], 0, false, true]]", MapboxPropertyType::Bool,
        &root_node));

    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Case, 3);
    {
        EXPECT_OPERATOR_NODE(ctx.node(root_node, 0), Node::Type::Equal, 2);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0, 0), "attr");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 0, 1), Value::from(0.0));
        }
        EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from(false));

        EXPECT_LITERAL_NODE(ctx.node(root_node, 2), Value::from(true));
    }

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"match\", [\"get\", \"attr\"], \"red\", \"#ff0000\", \"green\", \"#00ff00\", \"blue\", "
        "\"#0000ff\", \"#000000\"]]",
        MapboxPropertyType::Color, &root_node));

    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Case, 7);
    {
        EXPECT_OPERATOR_NODE(ctx.node(root_node, 0), Node::Type::Equal, 2);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0, 0), "attr");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 0, 1), Value::from("red"));
        }
        EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from((uint64_t)0xff0000ff));

        EXPECT_OPERATOR_NODE(ctx.node(root_node, 2), Node::Type::Equal, 2);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 2, 0), "attr");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 2, 1), Value::from("green"));
        }
        EXPECT_LITERAL_NODE(ctx.node(root_node, 3), Value::from((uint64_t)0xff00ff00));

        EXPECT_OPERATOR_NODE(ctx.node(root_node, 4), Node::Type::Equal, 2);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 4, 0), "attr");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 4, 1), Value::from("blue"));
        }
        EXPECT_LITERAL_NODE(ctx.node(root_node, 5), Value::from((uint64_t)0xffff0000));

        EXPECT_LITERAL_NODE(ctx.node(root_node, 6), Value::from((uint64_t)0xff000000));
    }

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"match\", [\"get\", \"attr\"], [\"Bicycle\", \"Duck\"], \"good\", [\"Car\", "
        "\"Goose\"], \"bad\", \"don't know\"]]",
        MapboxPropertyType::String, &root_node));

    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Case, 5);
    {
        EXPECT_OPERATOR_NODE(ctx.node(root_node, 0), Node::Type::Or, 2);
        {
            EXPECT_OPERATOR_NODE(ctx.node(root_node, 0, 0), Node::Type::Equal, 2);
            {
                EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0, 0, 0), "attr");
                EXPECT_LITERAL_NODE(ctx.node(root_node, 0, 0, 1), Value::from("Bicycle"));
            }

            EXPECT_OPERATOR_NODE(ctx.node(root_node, 0, 1), Node::Type::Equal, 2);
            {
                EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0, 1, 0), "attr");
                EXPECT_LITERAL_NODE(ctx.node(root_node, 0, 1, 1), Value::from("Duck"));
            }
        }
        EXPECT_LITERAL_NODE(ctx.node(root_node, 1), Value::from("good"));

        EXPECT_OPERATOR_NODE(ctx.node(root_node, 2), Node::Type::Or, 2);
        {
            EXPECT_OPERATOR_NODE(ctx.node(root_node, 2, 0), Node::Type::Equal, 2);
            {
                EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 2, 0, 0), "attr");
                EXPECT_LITERAL_NODE(ctx.node(root_node, 2, 0, 1), Value::from("Car"));
            }

            EXPECT_OPERATOR_NODE(ctx.node(root_node, 2, 1), Node::Type::Equal, 2);
            {
                EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 2, 1, 0), "attr");
                EXPECT_LITERAL_NODE(ctx.node(root_node, 2, 1, 1), Value::from("Goose"));
            }
        }
        EXPECT_LITERAL_NODE(ctx.node(root_node, 3), Value::from("bad"));

        EXPECT_LITERAL_NODE(ctx.node(root_node, 4), Value::from("don't know"));
    }

    EXPECT_RESULT_NOK(
        ctx.parse_expression("[[\"match\"]]", MapboxPropertyType::Number, &root_node));
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"match\", [\"get\", \"attr\"], 0, false, 1, true]]", MapboxPropertyType::Bool,
        &root_node));
    EXPECT_RESULT_NOK(ctx.parse_expression(
        "[[\"match\", [\"get\", \"attr\"], 42, \"number\", \"forty-two\", \"string\", \"don't "
        "know\"]]",
        MapboxPropertyType::String, &root_node));
}

TEST(MapboxExpression, parse_in_operator_string_attribute)
{
    ParseContext ctx;
    NodeIndex root_node;

    std::array<const char*, 3> refs{"ref1", "ref2", "ref3"};

    auto check_expr = [&](const char* expr)
    {
        ctx.attributes.clear();
        EXPECT_RESULT_OK(ctx.parse_expression(expr, MapboxPropertyType::Bool, &root_node));

        EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Or, 3);

        for (int i = 0; i < 3; i++)
        {
            EXPECT_OPERATOR_NODE(ctx.node(root_node, i), Node::Type::Equal, 2);
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, i, 0), "attr");
            EXPECT_LITERAL_NODE(ctx.node(root_node, i, 1), Value::from(refs.at(i)));
        }
    };

    {
        SCOPED_TRACE("against strings");
        check_expr("[[\"in\", [\"get\", \"attr\"], \"ref1\", \"ref2\", \"ref3\"]]");
    }

    {
        SCOPED_TRACE("implicit get");
        check_expr("[[\"in\", \"attr\", \"ref1\", \"ref2\", \"ref3\"]]");
    }

    {
        SCOPED_TRACE("against string array");
        check_expr(
            "[[\"in\", [\"get\", \"attr\"], [\"literal\", [\"ref1\", \"ref2\", \"ref3\"]]]]");
    }
}

TEST(MapboxExpression, swap_nested_cases)
{
    ParseContext ctx;
    uint32_t root_node;

    // Complex version of "if (x == 5 and y == 5)"
    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"all\", [\"==\", [\"get\", \"x\"], 5], [\"case\", [\"==\", [\"get\", \"y\"], 5], true, "
        "false]]]",
        MapboxPropertyType::Bool, &root_node));

    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Case, 3);
    {
        EXPECT_OPERATOR_NODE(ctx.node(root_node, 0), Node::Type::Equal, 2);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0, 0), "y");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 0, 1), Value::from(5.0));
        }

        EXPECT_OPERATOR_NODE(ctx.node(root_node, 1), Node::Type::And, 2);
        {
            EXPECT_OPERATOR_NODE(ctx.node(root_node, 1, 0), Node::Type::Equal, 2);
            {
                EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 1, 0, 0), "x");
                EXPECT_LITERAL_NODE(ctx.node(root_node, 1, 0, 1), Value::from(5.0));
            }
            EXPECT_LITERAL_NODE(ctx.node(root_node, 1, 1), Value::from(true));
        }

        EXPECT_OPERATOR_NODE(ctx.node(root_node, 2), Node::Type::And, 2);
        {
            EXPECT_OPERATOR_NODE(ctx.node(root_node, 2, 0), Node::Type::Equal, 2);
            {
                EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 2, 0, 0), "x");
                EXPECT_LITERAL_NODE(ctx.node(root_node, 2, 0, 1), Value::from(5.0));
            }
            EXPECT_LITERAL_NODE(ctx.node(root_node, 2, 1), Value::from(false));
        }
    }

    // Cases within case conditions are problematic:
    // They should be moved to the outputs, so that we can transform them into nested if statements.
    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"case\", [\"case\", [\"==\", [\"get\", \"x\"], 5], [\"get\", \"bool1\"], [\"get\", "
        "\"bool2\"]], true, false]]",
        MapboxPropertyType::Bool, &root_node));

    EXPECT_OPERATOR_NODE(ctx.node(root_node), Node::Type::Case, 3);
    {
        EXPECT_OPERATOR_NODE(ctx.node(root_node, 0), Node::Type::Equal, 2);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 0, 0), "x");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 0, 1), Value::from(5.0));
        }

        EXPECT_OPERATOR_NODE(ctx.node(root_node, 1), Node::Type::Case, 3);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 1, 0), "bool1");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 1, 1), Value::from(true));
            EXPECT_LITERAL_NODE(ctx.node(root_node, 1, 2), Value::from(false));
        }

        EXPECT_OPERATOR_NODE(ctx.node(root_node, 2), Node::Type::Case, 3);
        {
            EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node, 2, 0), "bool2");
            EXPECT_LITERAL_NODE(ctx.node(root_node, 2, 1), Value::from(true));
            EXPECT_LITERAL_NODE(ctx.node(root_node, 2, 2), Value::from(false));
        }
    }
}

TEST(MapboxExpression, parse_unsupported_interpolate)
{
    std::string expr_tpl =
        "[[\"interpolate\", TYPE, [\"get\", \"input\"], 1, \"o1\", 2, \"o2\", 3, \"o3\"]]";

    size_t type_pos = expr_tpl.find("TYPE");
    size_t type_size = strlen("TYPE");
    ASSERT_NE(type_pos, std::string::npos);

    const char* types[] = {
        "[\"linear\"]", "[\"exponential\", 2]", "[\"cubic-bezier\", 1, 0, 0, 1]"};

    for (auto type : types)
    {
        SCOPED_TRACE(type);

        ParseContext ctx;
        uint32_t root_node;

        std::string expr = expr_tpl;
        expr.replace(type_pos, type_size, type);

        EXPECT_RESULT_OK(
            ctx.parse_expression(expr.c_str(), MapboxPropertyType::String, &root_node));
        EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from("o1"));

        size_t o1_pos = expr.find("\"o1\"");
        size_t o1_size = strlen("\"o1\"");
        ASSERT_NE(o1_pos, std::string::npos);

        expr.replace(o1_pos, o1_size, "[\"get\", \"o1\"]");

        EXPECT_RESULT_OK(
            ctx.parse_expression(expr.c_str(), MapboxPropertyType::String, &root_node));
        EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node), "o1");
    }
}

TEST(MapboxExpression, parse_unsupported_step)
{
    ParseContext ctx;
    uint32_t root_node;

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"step\", [\"get\", \"input\"], \"o0\", 1, \"o1\", 2, \"o2\"]]",
        MapboxPropertyType::String, &root_node));
    EXPECT_LITERAL_NODE(ctx.node(root_node), Value::from("o0"));

    EXPECT_RESULT_OK(ctx.parse_expression(
        "[[\"step\", [\"get\", \"input\"], [\"get\", \"o0\"], 1, \"o1\", 2, \"o2\"]]",
        MapboxPropertyType::String, &root_node));
    EXPECT_ATTRIBUTE_NODE(ctx, ctx.node(root_node), "o0");
}

TEST(MapboxExpression, parse_color)
{
    static const char* expressions[] = {
        // Short RGB hexadecimal colors.
        "#000", "#666", "#777", "#888", "#fff",

        // RGB hexadecimal colors.
        "#000000", "#123456", "#789abc", "#def000", "#ffffff",

        // rgb() function colors.
        "rgb(0, 0, 0)", "rgb(255, 0, 0)", "rgb(0, 0, 255)", "rgb(0, 255, 0)", "rgb(255, 255, 0)",
        "rgb(0, 255, 255)", "rgb(128, 128, 128)", "rgb(255, 255, 255)",

        // rgba() function colors.
        "rgba(0, 0, 0, 0.5020)", "rgba(255, 255, 0, 0.2510)", "rgba(0, 255, 255, 0.7529)",

        // hsl() function colors.
        "hsl(180, 50%, 50%)", "hsl(0, 100%, 50%)", "hsl(240, 100%, 50%)", "hsl(100, 0%, 0%)",
        "hsl(200, 0%, 0%)", "hsl(100, 0%, 100%)", "hsl(200, 0%, 100%)", "hsl(192, 48%, 16%)",

        // hsla() function colors.
        "hsla(0, 0%, 0%, 0)", "hsla(0, 0%, 0%, 0.4)", "hsla(0, 0%, 0%, 0.6)", "hsla(0, 0%, 0%, 1)",

        // HTML color names
        "yellow", "turquoise", "sienna"};

    static const uint32_t expected_colors[] = {
        // Short RGB hexadecimal colors.
        0xff000000,
        0xff666666,
        0xff777777,
        0xff888888,
        0xffffffff,

        // RGB hexadecimal colors.
        0xff000000,
        0xff563412,
        0xffbc9a78,
        0xff00f0de,
        0xffffffff,

        // rgb() function colors.
        0xff000000,
        0xff0000ff,
        0xffff0000,
        0xff00ff00,
        0xff00ffff,
        0xffffff00,
        0xff808080,
        0xffffffff,

        // rgba() function colors.
        0x80000000,
        0x4000ffff,
        0xC0ffff00,

        // hsl() function colors.
        0xffbfbf40,
        0xff0000ff,
        0xffff0000,
        0xff000000,
        0xff000000,
        0xffffffff,
        0xffffffff,
        0xff3c3515,

        // hsla() function colors.
        0x00000000,
        0x66000000,
        0x99000000,
        0xff000000,

        // HTML color names
        0xff00ffff,
        0xffd0e040,
        0xff2d52a0,
    };

    size_t size = sizeof(expressions) / sizeof(expressions[0]);
    size_t size2 = sizeof(expected_colors) / sizeof(expected_colors[0]);

    ASSERT_EQ(size, size2);

    for (size_t i = 0; i < size; ++i)
    {
        SCOPED_TRACE(expressions[i]);

        uint32_t color;
        ASSERT_TRUE(hrz_mapbox::parse_mapbox_color_string(expressions[i], &color));
        ASSERT_EQ(color, expected_colors[i]);
    }
}

TEST(MapboxExpression, parse_invalid_color)
{
    static const char* expressions[] = {
        "",
        "#",
        "#f",
        "#ffff",
        "#fffffff",
        "rgb(",
        "rgba(",
        "hsl(",
        "hsla(",
        "rgb()",
        "rgb(10 10)",
        "rgb(10, 10)",
        "rgb(10, 10, 10, 10)",
        "rgba(10)",
        "rgba(10, 10, 10, 10, 10)",
        "hsl(1)",
        "hsl(1 10%)",
        "hsl(1, 2%, 2%, 1)",
        "hsl(1, 2, 2%, 1)",
        "hsl(1, 2%, 2)",
        "hsl(1, 2%, 2)",
        "hsla(1)",
        "hsla(1, 1%, 1%, 1, 1)",
    };

    size_t size = sizeof(expressions) / sizeof(expressions[0]);

    for (size_t i = 0; i < size; ++i)
    {
        SCOPED_TRACE(expressions[i]);

        uint32_t color;
        ASSERT_FALSE(hrz_mapbox::parse_mapbox_color_string(expressions[i], &color));
    }
}
