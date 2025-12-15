#include "hrz/core/jobs/styling.h"

#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/random.h"
#include "hrz/common/style/operator_evaluator.h"
#include "hrz/common/vector_data/packed_attribute_values_builder.h"
#include "hrz/core/jobs/context.h"
#include "hrz/core/jobs/job_result.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_index_pool.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/static_vector.h"

#include <fmt/args.h>

#define CHECK_ERR_M(MSG, ...)   \
    do                          \
    {                           \
        if (!(__VA_ARGS__))     \
        {                       \
            HRZ_LOG_ERROR(MSG); \
            return false;       \
        }                       \
    } while (0)

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
using namespace hrz::vector_data;
using namespace hrz::style;
using namespace hrz_jobs;

struct Instance
{
    // Index of the feature in feature_ids and in the attributes data.
    size_t feature_index{};

    hrz::flat_hash_map<uint64_t, PackedAttributeValue> prps;

    void set(uint64_t prp_id, PackedAttributeValue value) { prps[prp_id] = value; }
};

struct BufferPool
{
    using Handle = uint32_t;

    BufferPool(BlobAllocator* ba, hrz::monitoring::ResourceOwner owner, uint32_t buffer_size) :
        _buffers(ba), _buffer_size(buffer_size)
    {
        _buffers.register_blob_owner(owner);
        _buffers.register_blob_metadata("contents"_ss, "styling buffers"_ss);
    }

    Handle request_buffer()
    {
        Handle handle = _handles.alloc();
        if (IndexPool::get_index(handle) >= _num_buffers)
        {
            _handles.release(handle);
            return (Handle)0;
        }
        _debug_num_requests += 1;
        return handle;
    }

    void release_buffer(Handle handle)
    {
        _handles.release(handle);
        _debug_num_releases += 1;
    }

    std::span<RawValue> buffer(Handle handle)
    {
        if (!_handles.is_valid(handle)) return {};

        uint32_t index = IndexPool::get_index(handle);
        assert(index < _num_buffers);

        auto span = _buffers.data();
        if (!span) return {};

        return span->subspan(index * _buffer_size, _buffer_size);
    }

    void grow_num_buffers(uint32_t num_buffers)
    {
        if (num_buffers <= _num_buffers) return;

        _num_buffers = num_buffers;
        _buffers.resize(_num_buffers * _buffer_size);
    }

    inline void check_stability() const { assert(_debug_num_requests == _debug_num_releases); }

private:
    using IndexPool = hrz::GenIndexPool<Handle, 16, 16>;

    IndexPool _handles;
    hrz::BlobVector<RawValue> _buffers;

    uint32_t _num_buffers = 0;
    uint32_t _buffer_size;

    uint32_t _debug_num_requests = 0;
    uint32_t _debug_num_releases = 0;
};

struct Value
{
    enum class Kind
    {
        Attribute,
        Constant,
        Palette,
        Variable,
    };

    Kind kind;

    union
    {
        RawValue constant;
        uint32_t palette_id;
        BufferPool::Handle buffer_handle;
    };

    static Value make_attribute(BufferPool::Handle buffer)
    {
        Value value{Kind::Attribute};
        value.buffer_handle = buffer;
        return value;
    }

    static Value make_constant(const RawValue& v)
    {
        Value value{Kind::Constant};
        value.constant = v;
        return value;
    }

    static Value make_palette(uint32_t palette)
    {
        Value value{Kind::Palette};
        value.palette_id = palette;
        return value;
    }

    static Value make_variable(BufferPool::Handle buffer)
    {
        Value value{Kind::Variable};
        value.buffer_handle = buffer;
        return value;
    }
};

struct State
{
    static constexpr size_t BatchSize = 64;
    static constexpr size_t InitialPrpsPerFeature = 8;

    struct InstanceSpan
    {
        size_t begin;
        size_t end; // 1 past last

        inline size_t size() const { return end - begin; }
    };

    struct VariableInfo
    {
        BufferPool::Handle buffer_handle;
    };

    struct Property
    {
        uint32_t index;
    };

    const FeaturesStylingData* data;
    hrz::flat_hash_map<uint32_t, PackedAttributeValuesReader> attribute_values;
    hrz::BlobVector<StyledFeatures::Instance> resp_instances;
    hrz::BlobVector<uint64_t> resp_prps;
    PackedAttributeValuesBuilder resp_values;

    std::vector<hrz::RngState> instances_rng_states;

    // Temporary storage used during expression evaluation.
    BufferPool buffer_pool;
    // Used for fmt. Using Arena would be better because this copies when
    // reallocating, but unless we could know in advance the size of the fmt
    // output, we can't use it.
    fmt::memory_buffer fmt_buffer;
    // Used for to_string.
    hrz::Arena tmp_arena;
    std::vector<hrz::RngState> batch_rng_states;

    OperatorEvaluator::Context operator_evaluator_context;
    HRZ_NO_UNIQUE_ADDRESS OperatorEvaluator operator_evaluator;

    // Index of each instance in "instances".
    // Allows reordering the instances without touching "instances", which is
    // expensive to reorder.
    // It may be reordered by branches or expanded by forks during evaluation.
    // We do this indirection because it allows us to address all instances
    // during control flow as a span.
    // For example, if we have instance [a, b, c] in inst_index, and the a branch is evaluated
    // as [true, false, true], we reorder inst_index to something like [b, a, c] so now
    // we can evaluated the "false" branch with the span [b], and the "true" branch with the
    // span [a, c].
    std::vector<size_t> inst_index;

    // Data per feature instance.
    // May be expanded by forks.
    std::vector<Instance> instances;

    hrz::flat_hash_map<hrz::uint128, uint32_t> representation_to_id;
    hrz::flat_hash_set<uint32_t> representations;

    State(
        const FeaturesStylingData& data_,
        hrz::BlobAllocator* blob_allocator,
        hrz::monitoring::ResourceOwner owner) :
        data(&data_),
        resp_instances(hrz::BlobVector<StyledFeatures::Instance>(
            blob_allocator,
            data->feature_count * data->representations.size())),
        resp_prps(
            hrz::BlobVector<uint64_t>(blob_allocator, data->feature_count * InitialPrpsPerFeature)),
        resp_values(data->feature_count * InitialPrpsPerFeature, blob_allocator, owner),
        buffer_pool(blob_allocator, owner, BatchSize)
    {
        for (auto& attribute : data->attributes)
        {
            attribute_values.emplace(attribute.first, attribute.second.get_reader());
        }
    }

    bool fetch_rng_states(InstanceSpan batch_inst_span)
    {
        if (batch_rng_states.capacity() < BatchSize)
        {
            batch_rng_states.resize(BatchSize);
        }

        batch_rng_states.resize(batch_inst_span.size());
        for (size_t i = 0; i < batch_inst_span.size(); ++i)
        {
            auto index = inst_index[batch_inst_span.begin + i];
            assert(index < instances_rng_states.size());
            batch_rng_states[i] = instances_rng_states[index];
        }

        operator_evaluator_context.rng_states = batch_rng_states;

        return true;
    }

    // After evaluating expressions, this is used to copy the rng states back to
    // the instances so that subsequent calls to random functions won't return
    // the same values.
    bool update_rng_states(InstanceSpan batch_inst_span)
    {
        assert(batch_rng_states.size() == batch_inst_span.size());

        batch_rng_states.resize(batch_inst_span.size());
        for (size_t i = 0; i < batch_inst_span.size(); ++i)
        {
            auto index = inst_index[batch_inst_span.begin + i];
            assert(index < instances_rng_states.size());
            instances_rng_states[index] = batch_rng_states[i];
        }

        return true;
    }

    std::optional<RawValue> fetch_uniform_value(uint32_t uniform_id)
    {
        auto uniform_it = data->uniforms.find(uniform_id);
        if (uniform_it == data->uniforms.end())
        {
            HRZ_LOG_ERROR("Unknown uniform id {}", uniform_id);
            return std::nullopt;
        }
        else
        {
            return attr_as_ref(uniform_it->second);
        }
    }

    bool fetch_property_values(
        InstanceSpan inst_span,
        const hrz::flat_hash_map<uint64_t, uint32_t>& property_id_to_local_index,
        std::vector<VariableInfo>& property_infos)
    {
        for (const auto& it : property_id_to_local_index)
        {
            uint32_t local_index = it.second;

            auto buffer_handle = buffer_pool.request_buffer();
            assert(buffer_handle);
            auto buffer = buffer_pool.buffer(buffer_handle);

            VariableInfo prp_info{};
            prp_info.buffer_handle = buffer_handle;
            property_infos[local_index] = prp_info;

            for (size_t i = 0; i < inst_span.size(); ++i)
            {
                uint64_t prp_id = it.first;
                const Instance& inst = instances[inst_index[inst_span.begin + i]];
                auto prp_it = inst.prps.find(prp_id);

                if (prp_it == std::end(inst.prps))
                {
                    auto default_prp_it = data->properties_default_values.find(prp_id);
                    assert(default_prp_it != std::end(data->properties_default_values));
                    buffer[i] = attr_as_ref(default_prp_it->second);
                }
                else
                {
                    auto out_of_line_data = resp_values.encoder().get_out_of_line_data();
                    if (out_of_line_data)
                    {
                        buffer[i] = attr_as_ref(prp_it->second, out_of_line_data.value());
                    }
                    else
                    {
                        buffer[i] = attr_null<RawValue>();
                    }
                }
            }
        }

        return true;
    }

    bool fetch_attribute_values(
        InstanceSpan inst_span,
        const hrz::flat_hash_map<uint32_t, uint32_t>& attribute_id_to_local_index,
        std::vector<VariableInfo>& attribute_infos)
    {
        for (const auto& it : attribute_id_to_local_index)
        {
            uint32_t attribute_id = it.first;
            auto attribute_it = attribute_values.find(attribute_id);
            uint32_t local_index = it.second;

            auto buffer_handle = buffer_pool.request_buffer();
            assert(buffer_handle);
            auto buffer = buffer_pool.buffer(buffer_handle);

            if (attribute_it != attribute_values.end())
            {
                for (size_t i = 0; i < inst_span.size(); ++i)
                {
                    const Instance& inst = instances[inst_index[inst_span.begin + i]];
                    buffer[i] = attribute_it->second.as_ref(inst.feature_index);
                }
            }
            else
            {
                std::ranges::fill(buffer, attr_null<RawValue>());
            }

            VariableInfo attr_info{};
            attr_info.buffer_handle = buffer_handle;
            attribute_infos[local_index] = attr_info;
        }
        return true;
    }

    bool execute_fmt(FlatAst::NodeRef node_ref, std::vector<Value>& stack, size_t inst_count)
    {
        assert(node_ref.get_operator_kind() == Operator::Fmt);

        uint32_t va_arg_count = node_ref.get_operator_operand_count() - 1;

        CHECK_ERR_M("Corrupted stack", va_arg_count + 1 <= stack.size());

        Value value_str = stack[stack.size() - va_arg_count - 1];
        assert(value_str.kind == Value::Kind::Constant);
        assert(attr_is_string(value_str.constant));

        fmt::string_view fmt_str(
            attr_get_string(value_str.constant).data(), attr_get_string(value_str.constant).size());
        fmt::dynamic_format_arg_store<fmt::format_context> arg_store;
        arg_store.reserve(va_arg_count, 0);

        BufferPool::Handle res_buffer_handle = buffer_pool.request_buffer();
        auto res_buffer = buffer_pool.buffer(res_buffer_handle).subspan(0, inst_count);

        // @Note: The format string can't be checked in Emscripten because exception support is
        // disabled by default and fmt throws an exception when a format string is invalid. fmt
        // declares the FMT_EXCEPTION macro that selects which behaviour to adopt with regard to
        // exceptions. When they are not supported, fmt will use an assertion rather than an
        // exception. We choose to check the format string in native builds but ignore this check
        // in web builds. Thanks to fmt internal magic the formatting doesn't crash the application
        // in web release builds when the format string isn't valid (in other build mode the
        // assertion triggers). It creates a slight discrepancy between native and web build since
        // a native build with a "{} {" format string won't display properly but the web build will.
        // To resume, native builds will never crash on a bad format string. Web builds can trigger
        // an assertion on debug mode with an invalid format string but will work correctly without
        // crashing in release mode no matter the format string validity.
        //      - qdebroise 16/03/2023
        // @Todo(HRZ-220): Remove the exception support in native builds. Doing this will make
        // native builds behave the same as Emscripten builds (crash in debug but fine in release).
#ifndef __EMSCRIPTEN__
        try
        {
#endif
            for (size_t i = 0; i < inst_count; ++i)
            {
                arg_store.clear();

                for (size_t p = stack.size() - va_arg_count; p < stack.size(); ++p)
                {
                    const Value& param = stack[p];
                    CHECK_ERR_M(
                        "fmt() only accepts non-constant expressions.",
                        param.kind == Value::Kind::Variable
                            || param.kind == Value::Kind::Attribute);

                    const auto& param_buffer = buffer_pool.buffer(param.buffer_handle);
                    const RawValue& var = param_buffer[i];
                    switch (attr_type(var))
                    {
                        case AttributeValueType::kNull:
                            arg_store.push_back(std::string_view{});
                            break;
                        case AttributeValueType::kBoolean:
                            arg_store.push_back(attr_get_bool(var) ? "true" : "false");
                            break;
                        case AttributeValueType::kNumber:
                            arg_store.push_back(attr_get_number(var));
                            break;
                        case AttributeValueType::kInt64:
                            arg_store.push_back(attr_get_int64(var));
                            break;
                        case AttributeValueType::kUint64:
                            arg_store.push_back(attr_get_uint64(var));
                            break;
                        case AttributeValueType::kString:
                            arg_store.push_back(attr_get_string(var));
                            break;
                        default: assert(!"Unhandled case"); break;
                    }
                }

                size_t begin = fmt_buffer.size();
                fmt::vformat_to(
                    std::back_inserter(fmt_buffer), fmt::runtime(fmt_str).str, arg_store);
                res_buffer[i] =
                    std::string_view(fmt_buffer.data() + begin, fmt_buffer.size() - begin);
            }
#ifndef __EMSCRIPTEN__
        }
        catch (const fmt::format_error& e)
        {
            HRZ_LOG_ERROR("Format string error: {}", e.what());
            return false;
        }
#endif

        // Pop all the arguments and the format string from the stack and destroy any buffer when
        // appropriate.
        for (size_t i = 0; i < va_arg_count + 1; ++i)
        {
            const auto& value = stack.back();
            if (value.kind == Value::Kind::Variable)
            {
                buffer_pool.release_buffer(value.buffer_handle);
            }
            stack.pop_back();
        }

        stack.push_back(Value::make_variable(res_buffer_handle));

        return true;
    }

    inline BufferPool::Handle get_buffer(const Value& v, uint32_t inst_count)
    {
        BufferPool::Handle buffer_handle = 0;

        if (v.kind == Value::Kind::Variable || v.kind == Value::Kind::Attribute)
        {
            buffer_handle = v.buffer_handle;
        }
        else if (v.kind == Value::Kind::Constant)
        {
            buffer_handle = buffer_pool.request_buffer();
            auto buffer = buffer_pool.buffer(buffer_handle);
            std::fill(buffer.begin(), buffer.begin() + inst_count, v.constant);
        }
        else if (v.kind == Value::Kind::Palette)
        {
            // Palettes are only used in 'colorize()' which always use a constant palette given in
            // the first member of the buffer. Therefore, we don't need to fill the whole buffer.
            buffer_handle = buffer_pool.request_buffer();
            auto buffer = buffer_pool.buffer(buffer_handle);
            buffer[0] = attr_from<RawValue>(v.palette_id);
        }

        return buffer_handle;
    }

    bool execute_operator(FlatAst::NodeRef node_ref, std::vector<Value>& stack, size_t inst_count)
    {
        StaticVector<std::span<const RawValue>, kMaxFunctionParameters> spans;
        StaticVector<BufferPool::Handle, kMaxFunctionParameters> temp_buffer_handles;

        uint8_t operand_count = node_ref.get_operator_operand_count();
        CHECK_ERR_M("Expression evaluation error", operand_count <= stack.size());
        CHECK_ERR_M("Too many parameters", operand_count <= kMaxFunctionParameters);

        spans.set_size(operand_count);

        std::optional<BufferPool::Handle> res_buffer_handle = std::nullopt;

        for (int i = 0; i < operand_count; ++i)
        {
            uint32_t index = operand_count - i - 1;
            Value value = stack.back();
            stack.pop_back();

            BufferPool::Handle buffer_handle = get_buffer(value, inst_count);
            auto buffer = buffer_pool.buffer(buffer_handle).subspan(0, inst_count);

            spans[index] = buffer;

            if (!res_buffer_handle.has_value() && value.kind == Value::Kind::Variable)
            {
                // Reuse an operand buffer as the result buffer.
                res_buffer_handle = buffer_handle;
            }
            else if (value.kind != Value::Kind::Attribute)
            {
                // We can release all the buffers allocated for the operands except the one with
                // the computation results and those with the attribute values.
                temp_buffer_handles.push_back(buffer_handle);
            }
        }

        if (!res_buffer_handle.has_value())
        {
            res_buffer_handle = buffer_pool.request_buffer();
        }

        std::span<RawValue> res_buffer =
            buffer_pool.buffer(res_buffer_handle.value()).subspan(0, inst_count);
        CHECK_ERR(operator_evaluator(
            operator_evaluator_context, node_ref.get_operator_kind(), spans, res_buffer));

        // Release temporary buffers.

        for (auto handle : temp_buffer_handles)
        {
            buffer_pool.release_buffer(handle);
        }

        // When doing computation with a variable the result is stored in one of the variable
        // buffer, erasing one operand of the calculation.
        stack.push_back(Value::make_variable(res_buffer_handle.value()));

        return true;
    }

    // This is called when a new batch of values has finished processing. When
    // only 1 values is returned, it should be copied for the whole instance
    // span. Otherwise each value corresponds to an instance in the span.
    using ValueExpressionCallback = std::function<bool(InstanceSpan, std::span<const RawValue>)>;

    bool evaluate_expr(
        const FlatAst::FullNode* root,
        InstanceSpan inst_span,
        const ValueExpressionCallback& callback)
    {
        HRZ_SCOPED_SAMPLE_A("Evaluate style expression");
        assert(root->kind == NodeKind::Expr);

        uint32_t offset = root->expr.offset;
        uint32_t size = root->expr.size;

        bool force_evaluation = false;
        bool must_fetch_rng_states = false;

        // List attributes necessary to evaluate the expression. When an attribute appears multiple
        // times in an expression its values are fetched only once.
        hrz::flat_hash_map<uint32_t, uint32_t> attribute_id_to_local_index;
        hrz::flat_hash_map<uint64_t, uint32_t> property_id_to_local_index;
        for (uint32_t i = offset; i < offset + size; ++i)
        {
            FlatAst::NodeRef node_ref = data->ast->expressions[i];
            if (node_ref.kind == NodeKind::Attribute)
            {
                auto it = attribute_id_to_local_index.find(node_ref.data);
                if (it != attribute_id_to_local_index.end()) continue;

                uint32_t local_index = attribute_id_to_local_index.size();
                attribute_id_to_local_index[node_ref.data] = local_index;

                force_evaluation = true;
            }
            else if (node_ref.kind == NodeKind::Property)
            {
                const auto& node = data->ast->full_nodes[node_ref.full_node];

                auto it = property_id_to_local_index.find(node.prp.id);
                if (it != property_id_to_local_index.end()) continue;

                uint32_t local_index = property_id_to_local_index.size();
                property_id_to_local_index[node.prp.id] = local_index;

                force_evaluation = true;
            }
            else if (
                node_ref.kind == NodeKind::Operator
                && is_random_function(node_ref.get_operator_kind()))
            {
                force_evaluation = true;
                must_fetch_rng_states = true;
            }
        }

        if (size == 1 && !force_evaluation)
        {
            // In case of a single constant we can avoid the per-batch evaluation and directly
            // process all the instances at once.

            FlatAst::NodeRef node_ref = data->ast->expressions[offset];
            if (node_ref.kind == NodeKind::Literal)
            {
                const FlatAst::FullNode& node = data->ast->full_nodes[node_ref.full_node];
                CHECK_ERR(callback(inst_span, {&node.literal, 1}));
            }
            else if (node_ref.kind == NodeKind::Uniform)
            {
                auto literal_opt = fetch_uniform_value(node_ref.data);
                CHECK_ERR(literal_opt.has_value());
                const auto& literal = literal_opt.value();
                CHECK_ERR(callback(inst_span, {&literal, 1}));
            }
            else
            {
                assert(!"Unexpected node kind");
            }

            return true;
        }

        // The evaluation process uses a stack to process the expression. The stack can have
        // references to an aside table for values that are instance-dependent. The aside table
        // stores attributes or intermediate results. Also, to avoid using too much memory we don't
        // evaluate all the instances at once, but per batch. This should improve data locality.

        // Evaluation stack.
        std::vector<Value> stack;
        std::vector<VariableInfo> attribute_infos;
        std::vector<VariableInfo> property_infos;
        attribute_infos.resize(attribute_id_to_local_index.size());
        property_infos.resize(property_id_to_local_index.size());
        // The buffer pool has temporary buffers available for evaluating the expression.
        // It initially stores the batch attributes needed for the computation. Note that an
        // attribute can be present multiple times in an expression in which case it also uses
        // multiple buffers. Beyond attributes, buffers store computation results that are instance-
        // dependent, constant values, output of random functions, etc.
        uint32_t buffer_count = kMaxFunctionParameters + attribute_id_to_local_index.size()
            + property_id_to_local_index.size() + 1;
        buffer_pool.grow_num_buffers(buffer_count);

        for (size_t batch_begin = inst_span.begin; batch_begin < inst_span.end;
             batch_begin += BatchSize)
        {
            size_t batch_end = std::min(batch_begin + BatchSize, inst_span.end);
            InstanceSpan batch_inst_span{batch_begin, batch_end};

            stack.clear();

            CHECK_ERR(fetch_attribute_values(
                batch_inst_span, attribute_id_to_local_index, attribute_infos));
            CHECK_ERR(
                fetch_property_values(batch_inst_span, property_id_to_local_index, property_infos));
            if (must_fetch_rng_states)
            {
                CHECK_ERR(fetch_rng_states(batch_inst_span));
            }

            for (uint32_t i = offset; i < offset + size; ++i)
            {
                FlatAst::NodeRef node_ref = data->ast->expressions[i];

                if (node_ref.kind == NodeKind::Literal)
                {
                    const FlatAst::FullNode* node = &data->ast->full_nodes[node_ref.full_node];
                    stack.push_back(Value::make_constant(node->literal));
                }
                else if (node_ref.kind == NodeKind::Uniform)
                {
                    auto literal_opt = fetch_uniform_value(node_ref.data);
                    CHECK_ERR(literal_opt.has_value());
                    const auto& literal = literal_opt.value();

                    Value value{Value::Kind::Constant};
                    value.constant = literal;
                    stack.push_back(value);
                }
                else if (node_ref.kind == NodeKind::Attribute)
                {
                    uint32_t local_index = attribute_id_to_local_index[node_ref.data];
                    VariableInfo attr_info = attribute_infos[local_index];
                    stack.push_back(Value::make_attribute(attr_info.buffer_handle));
                }
                else if (node_ref.kind == NodeKind::Property)
                {
                    const auto& node = data->ast->full_nodes[node_ref.full_node];

                    uint32_t local_index = property_id_to_local_index[node.prp.id];
                    VariableInfo prp_info = property_infos[local_index];

                    Value value{Value::Kind::Variable};
                    value.buffer_handle = prp_info.buffer_handle;
                    stack.push_back(value);
                }
                else if (node_ref.kind == NodeKind::Palette)
                {
                    uint32_t palette_id = node_ref.data;
                    assert(palette_id < data->palettes.size());
                    stack.push_back(Value::make_palette(palette_id));
                }
                else if (node_ref.kind == NodeKind::Operator)
                {
                    if (node_ref.get_operator_kind() == Operator::Fmt)
                    {
                        CHECK_ERR(execute_fmt(node_ref, stack, batch_inst_span.size()));
                    }
                    else
                    {
                        CHECK_ERR(execute_operator(node_ref, stack, batch_inst_span.size()));
                    }
                }
                else
                {
                    assert(!"Unhandled node kind");
                }
            }

            // RNG state must be updated before the callback with the evaluated expression, because
            // the callback can do anything, including modify the order of instances (for example
            // in the case of a branch condition), and modify this would mean we update
            // the RNG state in the wrong order.
            if (must_fetch_rng_states)
            {
                CHECK_ERR(update_rng_states(batch_inst_span));
            }

            CHECK_ERR_M("Expression evaluation error", stack.size() == 1);
            if (stack[0].kind == Value::Kind::Constant)
            {
                CHECK_ERR(callback(batch_inst_span, {&stack[0].constant, 1}));
            }
            else
            {
                auto buffer_handle = stack[0].buffer_handle;
                auto buffer = buffer_pool.buffer(buffer_handle);
                auto val_span = buffer.subspan(0, batch_inst_span.size());
                CHECK_ERR(callback(batch_inst_span, val_span));
            }

            // Release the last buffer storing the batch computation results as well as all the
            // attribute buffers.
            if (stack[0].kind != Value::Kind::Attribute)
            {
                buffer_pool.release_buffer(stack[0].buffer_handle);
            }
            for (const auto& attr_info : attribute_infos)
            {
                buffer_pool.release_buffer(attr_info.buffer_handle);
            }

            buffer_pool.check_stability();
            fmt_buffer.clear();
        }

        return true;
    }

    void emit_instances(InstanceSpan inst_span, uint32_t repr_id)
    {
        if (representations.find(repr_id) == representations.end())
        {
            // We don't fail, we just don't emit anything.
            return;
        }

        for (size_t i = inst_span.begin; i < inst_span.end; ++i)
        {
            size_t inst_id = inst_index[i];
            Instance& inst = instances[inst_id];

            StyledFeatures::Instance inst_emit{};
            inst_emit.feature_index = inst.feature_index;
            inst_emit.repr_id = repr_id;
            inst_emit.first_prp = resp_prps.size().value_or(0);
            inst_emit.prp_count = inst.prps.size();
            resp_instances.push_back(inst_emit);

            for (const auto& p : inst.prps)
            {
                resp_prps.push_back(p.first);
                resp_values.push_encoded(
                    unsafe("properties values are interned using the resp_values encoder "
                           "(execute_set)"),
                    p.second);
            }
        }
    }

    void emit_instances(InstanceSpan inst_span, std::string_view name)
    {
        hrz::uint128 hash = hrz::murmur3_x64_128(name);
        auto it = representation_to_id.find(hash);
        if (it != representation_to_id.end())
        {
            emit_instances(inst_span, it->second);
        }
    }

    void emit_instances(InstanceSpan inst_span, std::span<const RawValue> values)
    {
        if (values.size() == 1)
        {
            if (attr_is_string(values[0]))
            {
                emit_instances(inst_span, attr_get_string(values[0]));
            }
            else
            {
                emit_instances(inst_span, (uint32_t)attr_as_uint64(values[0]));
            }
        }
        else
        {
            assert(inst_span.size() == values.size());
            for (size_t i = 0; i < inst_span.size(); ++i)
            {
                InstanceSpan this_span{};
                this_span.begin = inst_span.begin + i;
                this_span.end = this_span.begin + 1;

                if (attr_is_string(values[i]))
                {
                    emit_instances(this_span, attr_get_string(values[i]));
                }
                else
                {
                    emit_instances(this_span, (uint32_t)attr_as_uint64(values[i]));
                }
            }
        }
    }

    bool execute_emit(FlatAst::NodeRef node_ref, InstanceSpan inst_span)
    {
        auto do_emit = [this](InstanceSpan batch_span, std::span<const RawValue> values)
        {
            emit_instances(batch_span, values);
            return true;
        };

        const FlatAst::FullNode* node = &data->ast->full_nodes[node_ref.data];
        switch (node->kind)
        {
            case NodeKind::Expr:
            {
                CHECK_ERR(evaluate_expr(node, inst_span, do_emit));
                break;
            }
            case NodeKind::Literal:
            {
                CHECK_ERR(do_emit(inst_span, {&node->literal, 1}));
                break;
            }
            default: assert(!"Unhandled case"); return false;
        }

        return true;
    }

    bool execute_set(FlatAst::NodeRef node_ref, InstanceSpan inst_span)
    {
        const FlatAst::FullNode* root = &data->ast->full_nodes[node_ref.full_node];
        uint64_t prp_id = root->set.prp_id;

        auto do_set = [this, prp_id](InstanceSpan batch_span, std::span<const RawValue> values)
        {
            if (values.size() == 1)
            {
                auto raw_value = resp_values.encoder().encode_ref(values[0]);
                for (size_t i = batch_span.begin; i < batch_span.end; ++i)
                {
                    Instance& inst = instances[inst_index[i]];
                    inst.set(prp_id, raw_value);
                }
            }
            else
            {
                CHECK_ERR(batch_span.size() == values.size());
                for (size_t i = 0; i < batch_span.size(); ++i)
                {
                    Instance& inst = instances[inst_index[batch_span.begin + i]];
                    inst.set(prp_id, resp_values.encoder().encode_ref(values[i]));
                }
            }

            return true;
        };

        const FlatAst::FullNode* node = &data->ast->full_nodes[root->set.expr];
        switch (node->kind)
        {
            case NodeKind::Expr:
            {
                CHECK_ERR(evaluate_expr(node, inst_span, do_set));
                break;
            }
            case NodeKind::Literal:
            {
                CHECK_ERR(do_set(inst_span, {&node->literal, 1}));
                break;
            }
            default: assert(!"Unhandled case"); return false;
        }

        return true;
    }

    // The first `count`elements of inst are tested in `mask`.
    // We want to put the true elements in `true_span` (before `inst`) and
    // the false elements in `false_span` (after `inst`). We also update
    // `inst_span` to it contains the elements not tested yet.
    //
    // Example:
    // Before: x x x x | a b c d e f g h | x x x x
    //         true_s  | inst            | false_s
    //
    // mask (count = 4): 0 1 0 1
    //
    // After:  x x x x d b | e d f g h | a c x x x x
    //         true_s      | inst      | false_s
    //
    // @Note Right now the output is a bit scrambled, which is not ideal.
    // We may want to sort each span at the end but I'm not sure it's worth it.
    // In the future we may want to evaluate that.
    //      -slerouzic, 2020-04-06
    void sort_true_false(
        InstanceSpan* inst,
        size_t count,
        uint64_t mask,
        InstanceSpan* true_span,
        InstanceSpan* false_span)
    {
        // We're gonna reorder the elements in `inst` so we need a way to still
        // link them to their bit position in `mask`.
        int bits[64] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                        32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
                        48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};

        // This is the span that contains the elements of the mask.
        std::span<int> bits_span(bits, count);

        for (; count > 0; --count)
        {
            int bit = bits_span[0];
            if (mask & (1ull << bit)) // This is a true
            {
                // Include it in the true span, and remove it from the instances span
                true_span->end += 1;
                inst->begin += 1;
                bits_span = bits_span.subspan(1);
            }
            else // This is a false
            {
                // Swap it with last elements that we are testing
                std::swap(bits_span[0], bits_span[bits_span.size() - 1]);
                std::swap(inst_index[inst->begin], inst_index[inst->begin + count - 1]);

                // Swap the last element that we are testing (that is now
                // the feature that we were originally iterating over) with
                // the last instance in `inst`, and remove it from the bits span.
                std::swap(inst_index[inst->begin + count - 1], inst_index[inst->end - 1]);
                bits_span = bits_span.subspan(0, bits_span.size() - 1);

                // Finally include it in the false span, and remove it from the instances span.
                false_span->begin -= 1;
                inst->end -= 1;
            }
        }
    }

    bool execute_branch(FlatAst::NodeRef node_ref, InstanceSpan* inst_span)
    {
        const FlatAst::FullNode* root = &data->ast->full_nodes[node_ref.full_node];

        const FlatAst::FullNode* cond_node = &data->ast->full_nodes[root->branch.cond];
        const FlatAst::FullNode* then_node = &data->ast->full_nodes[root->branch.then_body];
        const FlatAst::FullNode* else_node =
            root->branch.else_body != 0 ? &data->ast->full_nodes[root->branch.else_body] : nullptr;

        InstanceSpan to_exec = *inst_span;
        InstanceSpan true_span{}, false_span{};
        true_span.begin = inst_span->begin;
        true_span.end = inst_span->begin;
        false_span.begin = inst_span->end;
        false_span.end = inst_span->end;

        std::vector<uint64_t> results;
        results.reserve(inst_span->size() / BatchSize + 1);

        switch (cond_node->kind)
        {
            case NodeKind::Expr:
            {
                while (to_exec.size() > 0)
                {
                    size_t begin = to_exec.begin;
                    size_t end = std::min(begin + BatchSize, to_exec.end);
                    InstanceSpan iter_span = {begin, end};

                    CHECK_ERR(evaluate_expr(
                        cond_node, iter_span,
                        [&](InstanceSpan batch_span, std::span<const RawValue> values) -> bool
                        {
                            // Evaluate as constant expression if and only if we have only 1 value
                            // (this is a property of "evaluate_expr" that constant expressions are
                            // sent as only 1 value) and we have more than 1 instance in the batch.
                            // If we had 1 instance in the batch, it could be the last instance to
                            // process in a span so we don't want to override the true and false
                            // spans in this case, and the "normal" path takes care of that
                            // normally. Here we short-circuit the whole condition evaluation by
                            // reseting the size of "to_exec" to 0, hence ending the loop.
                            if (values.size() == 1 && batch_span.size() > 1)
                            {
                                bool val = attr_as_bool(values[0]);
                                if (val)
                                {
                                    true_span = to_exec;
                                }
                                else
                                {
                                    false_span = to_exec;
                                }

                                to_exec.end = to_exec.begin;
                            }
                            else
                            {
                                assert(batch_span.size() == values.size());
                                uint64_t result = 0;
                                for (size_t i = 0; i < batch_span.size(); ++i)
                                {
                                    if (attr_as_bool(values[i]))
                                    {
                                        result |= ((uint64_t)1 << i);
                                    }
                                }

                                assert(batch_span.size() == iter_span.size());
                                sort_true_false(
                                    &to_exec, batch_span.size(), result, &true_span, &false_span);
                            }

                            return true;
                        }));
                }

                break;
            }
            case NodeKind::Literal:
            {
                // Constant branch condition.
                if (attr_as_bool(cond_node->literal))
                {
                    true_span.end = inst_span->end;
                }
                else
                {
                    false_span.begin = inst_span->begin;
                }

                break;
            }
            default: assert(!"Unhandled case"); return false;
        }

        assert(true_span.size() + false_span.size() == inst_span->size());

        // Dispatch to 'then' and 'else' branches.
        if (true_span.size() > 0)
        {
            CHECK_ERR(execute_block(then_node, &true_span));
            // Some instances in the true_span were terminated, so we need to offset the false_span
            // so that all active instances remain contiguous in inst_span.
            if (true_span.end < false_span.begin)
            {
                size_t size = false_span.size();
                size_t* true_span_end = inst_index.data() + true_span.end;
                size_t* false_span_begin = inst_index.data() + false_span.begin;
                for (size_t i = 0; i < size; ++i)
                {
                    *true_span_end++ = *false_span_begin++;
                }
                false_span = {true_span.end, true_span.end + size};
                inst_span->end = false_span.end;
            }
        }

        if (else_node && false_span.size() > 0)
        {
            CHECK_ERR(execute_block(else_node, &false_span));
            // Some instances in the false_span may be terminated as well. In this case we don't
            // need to offset anything since the false_span is already at the end of inst_span.
            inst_span->end = false_span.end;
        }

        return true;
    }

    bool execute_fork(FlatAst::NodeRef node_ref, InstanceSpan inst_span)
    {
        // We duplicate all instances to fork them.
        size_t previous_size = inst_index.size();
        size_t previous_instances_size = instances.size();

        size_t count = inst_span.size();
        InstanceSpan fork_span{previous_size, previous_size + count};

        inst_index.resize(previous_size + count);
        instances.resize(previous_instances_size + count);
        instances_rng_states.resize(previous_instances_size + count);

        for (size_t i = 0; i < count; ++i)
        {
            size_t index = previous_instances_size + i;
            inst_index[fork_span.begin + i] = index;
            instances[index] = instances[inst_index[inst_span.begin + i]];
            instances_rng_states[index] = instances_rng_states[inst_index[inst_span.begin + i]];
        }

        const FlatAst::FullNode* block_node = &data->ast->full_nodes[node_ref.data];
        CHECK_ERR(execute_block(block_node, &fork_span));

        // Then we get rid of unused instances. It's essentially a big stack.
        inst_index.resize(previous_size);
        instances.resize(previous_instances_size);
        instances_rng_states.resize(previous_instances_size);

        return true;
    }

    bool execute_block(const FlatAst::FullNode* root, InstanceSpan* inst_span)
    {
        uint32_t stmt_begin = root->block.begin;
        uint32_t stmt_end = root->block.end;

        for (uint32_t i = stmt_begin; i < stmt_end; ++i)
        {
            FlatAst::NodeRef node_ref = data->ast->statements[i];
            switch (node_ref.kind)
            {
                case NodeKind::Emit:
                    CHECK_ERR(execute_emit(node_ref, *inst_span));
                    inst_span->end = inst_span->begin;
                    break;
                case NodeKind::Discard: inst_span->end = inst_span->begin; return true;
                case NodeKind::Fork: CHECK_ERR(execute_fork(node_ref, *inst_span)); break;
                case NodeKind::Set: CHECK_ERR(execute_set(node_ref, *inst_span)); break;
                case NodeKind::Block:
                {
                    const FlatAst::FullNode* node = &data->ast->full_nodes[node_ref.full_node];
                    CHECK_ERR(execute_block(node, inst_span));
                    break;
                }
                case NodeKind::Branch: CHECK_ERR(execute_branch(node_ref, inst_span)); break;
                default: CHECK_ERR_M("Unknown instruction", false); break;
            }
        }

        return true;
    }

    bool execute(InstanceSpan* inst_span)
    {
        if (data->ast->full_nodes.empty()) return true;

        const auto* root = &data->ast->full_nodes[data->ast->root];
        CHECK_ERR(execute_block(root, inst_span));

        return true;
    }
};
} // anonymous namespace

namespace hrz_jobs::style_features
{
hrz_jobs::JobResult run(
    const FeaturesStylingData& params,
    StylingResult& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("Style features");

    State state(params, context.get_blob_allocator(), context.get_resource_owner());

    for (const auto& repr : params.representations)
    {
        bool repr_id_already_registered = !state.representations.insert(repr.id).second;
        if (repr_id_already_registered)
        {
            HRZ_LOG_ERROR("Duplicate representation ID. Behaviour is undefined now.");
        }

        if (!repr.name.empty())
        {
            hrz::uint128 hash = hrz::murmur3_x64_128(repr.name);
            bool repr_name_already_registered =
                !state.representation_to_id.insert(std::make_pair(hash, repr.id)).second;
            if (repr_name_already_registered)
            {
                HRZ_LOG_ERROR("Duplicate representation name. Behaviour is undefined now.");
            }
        }
    }

    size_t feature_count = params.feature_count;

#ifndef NDEBUG
    for (const auto& attribute : params.attributes)
    {
        assert(attribute.second.values.size() == feature_count);
    }
#endif

    state.inst_index.resize(feature_count);
    state.instances.resize(feature_count);
    for (size_t i = 0; i < feature_count; ++i)
    {
        state.inst_index[i] = i;
        state.instances[i].feature_index = i;
    }

    auto feature_ids_hashes_data = state.data->feature_ids_hashes.get_data();
    state.instances_rng_states.resize(feature_count);
    if (!feature_ids_hashes_data.empty())
    {
        for (size_t i = 0; i < feature_ids_hashes_data.size(); ++i)
        {
            auto fid_hash = feature_ids_hashes_data[i];
            hrz::random::srand(state.instances_rng_states[i], fid_hash, state.data->rng_seed);
        }
    }
    else
    {
        for (size_t i = 0; i < feature_count; ++i)
        {
            auto feature_index = state.instances[i].feature_index;
            hrz::random::srand(state.instances_rng_states[i], feature_index, state.data->rng_seed);
        }
    }

    state.operator_evaluator_context.palettes = state.data->palettes;
    state.operator_evaluator_context.rng_states = state.batch_rng_states;
    state.operator_evaluator_context.arena = &state.tmp_arena;

    State::InstanceSpan to_exec{0, feature_count};
    if (state.execute(&to_exec))
    {
        auto resp_instances_data = state.resp_instances.data();
        if (resp_instances_data.has_value())
        {
            for (const auto& instance : resp_instances_data.value())
            {
                response.unique_reprs.insert(instance.repr_id);
            }
        }

        auto instances_opt = state.resp_instances.to_blob_array();
        auto prps_opt = state.resp_prps.to_blob_array();
        auto values_opt = state.resp_values.finalize(0);

        if (!instances_opt.has_value() || !prps_opt.has_value() || !values_opt.has_value())
        {
            HRZ_LOG_ERROR("Could not allocate data");
            return hrz_jobs::JobResult::FAILURE;
        }

        response.features.instances = std::move(instances_opt.value());
        response.features.prps = std::move(prps_opt.value());
        response.features.values = std::move(values_opt.value().values);
        response.features.out_of_line_data = std::move(values_opt.value().out_of_line_data);

        response.features.instances.register_blob_metadata(
            context.get_blob_allocator(), "contents"_ss, "styled features instances"_ss);
        response.features.prps.register_blob_metadata(
            context.get_blob_allocator(), "contents"_ss, "styled features properties"_ss);
        response.features.values.register_blob_metadata(
            context.get_blob_allocator(), "contents"_ss, "styled features values"_ss);
        response.features.out_of_line_data.register_blob_metadata(
            context.get_blob_allocator(), "contents"_ss, "styled features strings"_ss);

        response.features.instances.register_blob_owner(
            context.get_blob_allocator(), context.get_resource_owner());
        response.features.prps.register_blob_owner(
            context.get_blob_allocator(), context.get_resource_owner());
        response.features.values.register_blob_owner(
            context.get_blob_allocator(), context.get_resource_owner());
        response.features.out_of_line_data.register_blob_owner(
            context.get_blob_allocator(), context.get_resource_owner());

        return hrz_jobs::JobResult::SUCCESS;
    }
    else
    {
        HRZ_LOG_ERROR("Error during styling script execution");
        return hrz_jobs::JobResult::FAILURE;
    }
}
} // namespace hrz_jobs::style_features
