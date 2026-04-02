#pragma once

#include "hrz/common/palette.h"
#include "hrz/common/random.h"
#include "hrz/common/style/defs.h"
#include "hrz/common/vector_data/attribute_type_ref.h"
#include "hrz/fnd/arena.h"

namespace hrz::style
{

using RawValue = vector_data::RefAttributeValue;

struct OperatorEvaluator
{
    struct Context
    {
        std::span<const hrz::Palette> palettes;
        std::span<hrz::RngState> rng_states;
        hrz::Arena* arena;
    };

    // @Todo(C++23) Use static operator()
    bool operator ()(
        Context& ctx,
        Operator op,
        std::span<const std::span<const RawValue>> arg_buffers,
        std::span<RawValue> res_buffer) const;
};

} // namespace hrz::style
