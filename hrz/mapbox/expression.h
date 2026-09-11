// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/fnd/defines.h"
#include "hrz/mapbox/common.h"

#include <rapidjson/document.h>

#include <forward_list>
#include <span>
#include <vector>

namespace hrz_mapbox
{

struct ExpressionContext
{
    // For logging
    const char* mapbox_layer_id;

    std::vector<Node> nodes;
    std::vector<hrz_proto::Palette> palettes;

    // Yes, this is scary, but we only ever have to generate string data for format strings,
    // so for now that makes only one entry at most per text-field property.
    std::forward_list<std::string> string_data;

    VectorSource::AttributeMap* attributes;

    // Creates an unused node at index 0 so that we can use the index 0 to mean "no node".
    ExpressionContext(const char* mapbox_layer_name, VectorSource::AttributeMap* attributes) :
        mapbox_layer_id(mapbox_layer_name), attributes(attributes)
    {
        nodes.resize(1);
    }

    NodeIndex add_node(Node::Type type, std::initializer_list<NodeIndex> children = {})
    {
        NodeIndex index = nodes.size();
        nodes.push_back({});
        nodes.back().type = type;
        for (auto child : children)
        {
            nodes.back().children.push_back(child);
        }
        return index;
    }

    NodeIndex add_literal(const Value& value)
    {
        NodeIndex index = add_node(Node::Type::Literal);
        nodes[index].literal = value;
        return index;
    }

    // Shallow copy
    NodeIndex copy_node(NodeIndex index)
    {
        NodeIndex copy_index = nodes.size();
        nodes.push_back(nodes.at(index));
        return copy_index;
    }
};

// Parses expressions or literals, except array literals.
// Returns 0 on failure.
NodeIndex parse_expression(
    const rapidjson::Value& root,
    MapboxPropertyType expected_type,
    ExpressionContext& ctx);

NodeIndex parse_array_literal(
    const rapidjson::Value& root,
    MapboxPropertyType expected_component_type,
    ExpressionContext& ctx);

// Recursively traverse and update the expression tree using the information available to the
// context.
// The function should be called once for each expression, once all expressions have been fully
// parsed, otherwise there may be missing information regarding the inferred types of attributes.
void finalize_expression(NodeIndex root, ExpressionContext&);

// Recursively traverse the expression tree and generate the styling script for whole expression
// starting from the given node.
void generate_sub_expression_script(
    std::span<const Node> nodes,
    NodeIndex node_index,
    std::string& script);

#if HRZ_DEBUG
void print_node_tree(NodeIndex root, const ExpressionContext& ctx);
#endif

} // namespace hrz_mapbox
