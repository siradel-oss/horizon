#pragma once

#include "hrz/common/style/defs.h"
#include "hrz/common/vector_data/attribute_type_ref.h"
#include "hrz/fnd/intern_string.h"

#include <cstdint>
#include <vector>

namespace hrz::style
{

using RawValue = vector_data::RefAttributeValue;

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

    hrz::InternString intern;

    uint32_t root;

    // Stores all the nodes of the AST.
    std::vector<FullNode> full_nodes;
    // Stores references to statement nodes ('set', 'emit', 'if', etc). contiguously. This is used
    // to make the instructions properly ordered. Then, executing a block boils down to iterating
    // this array over the instruction span of a block.
    std::vector<NodeRef> statements;
    // Expressions are flattened while building the AST. An expression is translated as a sequence
    // of nodes representing its reverse polish notation and then stored contiguously into this
    // array. AST nodes can then reference expressions by an offset and a size.
    std::vector<NodeRef> expressions;

    inline uint32_t push_full_node(const FullNode& node)
    {
        full_nodes.push_back(node);
        return full_nodes.size() - 1;
    }
};

} // namespace hrz::style
