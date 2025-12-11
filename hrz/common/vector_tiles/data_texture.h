#pragma once

#include "hrz/common/shader_defines.h"

#include <lin_maths.h>

namespace hrz::vt
{

static constexpr uint32_t DATA_TEXTURE_SIZE = HRZ_S_VECTOR_REPR_DATA_TEXTURE_WIDTH;

inline lm::uvec2 compute_data_texture_size(uint32_t entry_count)
{
    if (entry_count == 0) return {0, 0};

    return {
        std::min(entry_count, DATA_TEXTURE_SIZE),
        std::max((entry_count - 1) / DATA_TEXTURE_SIZE + 1, (uint32_t)1)};
}
} // namespace hrz::vt
