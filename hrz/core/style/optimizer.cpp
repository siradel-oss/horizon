#include "hrz/common/profiling.h"
#include "hrz/core/style/script.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/static_vector.h"

#include <cassert>

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

struct OptimizerImpl : public Optimizer
{
    OperatorEvaluator::Context operator_evaluator_context;
    HRZ_NO_UNIQUE_ADDRESS OperatorEvaluator operator_evaluator;

    explicit OptimizerImpl(std::span<const hrz::Palette> palettes)
    {
        operator_evaluator_context.palettes = palettes;
    }

    ~OptimizerImpl() override = default;

    // Returns the expression node index.
    bool optimize_expr(Ast& ast, FlatAst& flat_ast, uint32_t root, uint32_t* new_root)
    {
        // We need a post-order traversal but it is easier to build a pre-order traversal array and
        // then reverse iterate it for the post-traversal. This is what the following loop is doing.

        std::vector<uint32_t> stack;
        stack.push_back(root);
        std::vector<uint32_t> preorder;

        while (!stack.empty())
        {
            uint32_t node_index = stack.back();
            const auto& node = ast.nodes[node_index];
            stack.pop_back();

            preorder.push_back(node_index);

            if (node.kind == NodeKind::Attribute || node.kind == NodeKind::Literal
                || node.kind == NodeKind::Palette || node.kind == NodeKind::Uniform
                || node.kind == NodeKind::Property)
            {
                // No-op, leaf node.
            }
            else
            {
                assert(node.kind == NodeKind::Operator);
                for (uint32_t i = 0; i < node.op.operand_count; ++i)
                {
                    stack.push_back(node.op.operand_exprs[i]);
                }
            }
        }

        // Fold constant expressions.

        auto is_subexpression_constant = [&](int index, uint32_t operand_count) -> bool
        {
            for (uint32_t i = 0; i < operand_count; ++i)
            {
                const auto& operand = ast.nodes[preorder[index + i]];
                if (operand.kind != NodeKind::Literal && operand.kind != NodeKind::Palette)
                {
                    // Search for attributes or operators.
                    // When an operator is encountered as an operand it means that an expression
                    // inspected earlier wasn't constant. This makes this expression non-constant as
                    // well.
                    return false;
                }
            }
            return true;
        };

        auto evaluate_operator = [&](int op_index, uint32_t operand_count, RawValue* res) -> bool
        {
            assert(operand_count <= kMaxFunctionParameters && "Increase kMaxFunctionParameters");

            StaticVector<RawValue, kMaxFunctionParameters> values;
            StaticVector<std::span<const RawValue>, kMaxFunctionParameters> spans;

            values.set_size(operand_count);
            spans.set_size(operand_count);

            for (uint32_t i = 0; i < operand_count; ++i)
            {
                uint32_t arg_index = operand_count - i - 1;
                const auto& node = ast.nodes[preorder[op_index + i + 1]];

                if (node.kind == NodeKind::Literal)
                {
                    spans[arg_index] = {&node.literal, 1};
                }
                else if (node.kind == NodeKind::Palette)
                {
                    // Put the palette ID inside a 'RawValue' so that it behaves like any other
                    // literal. The operators have the knowledge about their arguments so they know
                    // whether they should interpret something in a 'RawValue' as a palette ID or as
                    // a true literal value.
                    RawValue value =
                        hrz::vector_data::attr_from<RawValue>(node.data.value); // Palette ID
                    values[arg_index] = value;
                    spans[arg_index] = {&values[arg_index], 1};
                }
                else
                {
                    assert("Node must have a constant value");
                    continue;
                }
            }

            return operator_evaluator(
                operator_evaluator_context, ast.nodes[preorder[op_index]].op.op, spans, {res, 1});
        };

        // Selects which nodes to inspect for constant expression folding.
        auto operator_node_can_be_folded = [](const Ast::Node& node) -> bool
        {
            if (node.kind != NodeKind::Operator)
            {
                return false;
            }

            // Bypass some operators where we don't want constant expression folding. Typically,
            // we don't care about fmt() being a constant. It's the user responsibilty to use a
            // plain string instead.
            return node.op.op != Operator::Fmt && !is_random_function(node.op.op);
        };

        for (int i = preorder.size() - 1; i >= 0; --i)
        {
            uint32_t node_index = preorder[i];
            const auto& node = ast.nodes[node_index];

            if (operator_node_can_be_folded(node))
            {
                uint32_t operand_count = node.op.operand_count;
                assert(i + operand_count < preorder.size());

                if (is_subexpression_constant(i + 1, operand_count))
                {
                    // Fold the constant expression.
                    RawValue literal;
                    if (!evaluate_operator(i, operand_count, &literal))
                    {
                        HRZ_LOG_ERROR("Constant expression evaluation error");
                        return false;
                    }

                    Ast::Node new_node(NodeKind::Literal);
                    new_node.literal = literal;

                    ast.nodes[node_index] = new_node;

                    // Shift the post-order traversal array to overwrite the operands.
                    int new_size = preorder.size() - operand_count;
                    for (int j = i + 1; j < new_size; ++j)
                    {
                        preorder[j] = preorder[j + operand_count];
                    }
                    preorder.resize(new_size);
                }
            }
        }

        // Flatten the expression.

        const auto& first_node = ast.nodes[preorder[0]];
        if (preorder.size() == 1 && first_node.kind == NodeKind::Literal)
        {
            // When there is a single constant value in the expression, directly emit a literal
            // node instead of an expression node pointing to a literal. So, the AST looks like
            // [Set]->[Literal] instead of [Set]->[Expr]->[Literal].
            FlatAst::FullNode full_node;
            full_node.kind = NodeKind::Literal;
            full_node.literal = first_node.literal;
            *new_root = flat_ast.push_full_node(full_node);
            return true;
        }

        uint32_t offset = flat_ast.expressions.size();
        for (int i = preorder.size() - 1; i >= 0; --i)
        {
            uint32_t node_index = preorder[i];
            const auto& node = ast.nodes[node_index];

            FlatAst::NodeRef node_ref{};

            if (node.kind == NodeKind::Attribute)
            {
                node_ref.kind = NodeKind::Attribute;
                node_ref.data = (uint32_t)node.data.value; // Attribute ID.
            }
            else if (node.kind == NodeKind::Literal)
            {
                FlatAst::FullNode full_node;
                full_node.kind = NodeKind::Literal;
                full_node.literal = node.literal;

                node_ref.kind = NodeKind::Literal;
                node_ref.full_node = flat_ast.push_full_node(full_node);
            }
            else if (node.kind == NodeKind::Palette)
            {
                node_ref.kind = NodeKind::Palette;
                node_ref.data = (uint32_t)node.data.value; // Palette ID.
            }
            else if (node.kind == NodeKind::Uniform)
            {
                node_ref.kind = NodeKind::Uniform;
                node_ref.data = (uint32_t)node.data.value;
            }
            else if (node.kind == NodeKind::Property)
            {
                FlatAst::FullNode full_node;
                full_node.kind = NodeKind::Property;
                full_node.prp.id = node.data.value;

                node_ref.kind = NodeKind::Property;
                node_ref.full_node = flat_ast.push_full_node(full_node);
            }
            else if (node.kind == NodeKind::Operator)
            {
                node_ref.kind = node.kind;
                node_ref.set_operator(node.op.op, node.op.operand_count);
            }
            else
            {
                assert(!"Unexpected node kind");
                node_ref.kind = node.kind;
                node_ref.data = 0;
            }

            flat_ast.push_expression(node_ref);
        }

        uint32_t size = flat_ast.expressions.size() - offset;
        FlatAst::FullNode full_node;
        full_node.kind = NodeKind::Expr;
        full_node.expr.offset = offset;
        full_node.expr.size = size;
        *new_root = flat_ast.push_full_node(full_node);
        return true;
    }

    // Returns the block node index.
    bool flatten_block(Ast& ast, FlatAst& flat_ast, uint32_t node_index, uint32_t* root)
    {
        Arena::Vec<FlatAst::NodeRef> instrs;
        while (node_index != Ast::kInvalidNodeIndex)
        {
            const auto& node = ast.nodes[node_index];
            switch (node.kind)
            {
                case NodeKind::Discard:
                {
                    flat_ast.arena.push(instrs, {NodeKind::Discard, {0}});

                    node_index = node.discard.next_instr;
                    break;
                }
                case NodeKind::Emit:
                {
                    uint32_t expr;
                    CHECK_ERR(optimize_expr(ast, flat_ast, node.emit.expr, &expr));
                    flat_ast.arena.push(instrs, {NodeKind::Emit, {expr}});

                    node_index = node.emit.next_instr;
                    break;
                }
                case NodeKind::Set:
                {
                    FlatAst::FullNode full_node;
                    full_node.kind = NodeKind::Set;
                    full_node.set.prp_id = node.set.prp_id;
                    CHECK_ERR(optimize_expr(ast, flat_ast, node.set.expr, &full_node.set.expr));

                    FlatAst::NodeRef node_ref;
                    node_ref.kind = NodeKind::Set;
                    node_ref.full_node = flat_ast.push_full_node(full_node);
                    flat_ast.arena.push(instrs, node_ref);

                    node_index = node.set.next_instr;
                    break;
                }
                case NodeKind::Fork:
                {
                    uint32_t block_index;
                    CHECK_ERR(flatten_block(ast, flat_ast, node.fork.block, &block_index));
                    flat_ast.arena.push(instrs, {NodeKind::Fork, {block_index}});

                    node_index = node.fork.next_instr;
                    break;
                }
                case NodeKind::Branch:
                {
                    FlatAst::FullNode full_node;
                    full_node.kind = NodeKind::Branch;
                    CHECK_ERR(
                        optimize_expr(ast, flat_ast, node.branch.cond, &full_node.branch.cond));
                    CHECK_ERR(flatten_block(
                        ast, flat_ast, node.branch.then_body, &full_node.branch.then_body));
                    if (node.branch.else_body != Ast::kInvalidNodeIndex)
                    {
                        CHECK_ERR(flatten_block(
                            ast, flat_ast, node.branch.else_body, &full_node.branch.else_body));
                    }
                    else
                    {
                        full_node.branch.else_body = 0;
                    }

                    FlatAst::NodeRef node_ref;
                    node_ref.kind = NodeKind::Branch;
                    node_ref.full_node = flat_ast.push_full_node(full_node);
                    flat_ast.arena.push(instrs, node_ref);

                    node_index = node.branch.next_instr;
                    break;
                }
                default: assert(!"Unexpected node kind"); break;
            }
        }

        uint32_t offset = flat_ast.statements.size();
        for (uint32_t i = 0; i < instrs.size(); ++i)
        {
            flat_ast.push_statement(instrs[i]);
        }

        FlatAst::FullNode full_node;
        full_node.kind = NodeKind::Block;
        full_node.block.begin = offset;
        full_node.block.end = offset + instrs.size();
        *root = flat_ast.push_full_node(full_node);
        return true;
    }

    bool optimize(Ast&& ast, FlatAst* flat_ast) override
    {
        HRZ_SCOPED_SAMPLE("Optimize AST");

        assert(flat_ast);
        if (!flat_ast) return false;

        if (!flatten_block(ast, *flat_ast, ast.root, &flat_ast->root))
        {
            return false;
        }
        flat_ast->intern = std::move(ast.intern);
        ast.intern = InternString();
        return true;
    }
};
} // anonymous namespace

namespace hrz::style
{
std::unique_ptr<Optimizer> Optimizer::create(std::span<const hrz::Palette> palettes)
{
    return std::make_unique<OptimizerImpl>(palettes);
}
} // namespace hrz::style
