#pragma once

#include "hrz/common/shader_defines.h"

#include <assert.h>
#include <lin_maths.h>

namespace hrz::vt
{

static constexpr uint32_t DATA_TEXTURE_SIZE = HRZ_S_VECTOR_REPR_DATA_TEXTURE_WIDTH;

inline lm::uvec2 compute_data_texture_size(uint32_t entry_count)
{
    if (entry_count == 0) return {0, 0};

    return {
        std::min(entry_count, DATA_TEXTURE_SIZE),
        std::max((entry_count - 1) / DATA_TEXTURE_SIZE + 1, (uint32_t)1)
    };
}

inline uint32_t compute_data_texture_array_size(uint32_t entry_count)
{
    const lm::uvec2 texture_size = compute_data_texture_size(entry_count);
    const uint32_t array_size = texture_size.x * texture_size.y;

    assert(array_size >= entry_count);
    assert(array_size % DATA_TEXTURE_SIZE == 0 || entry_count < DATA_TEXTURE_SIZE);

    return array_size;
}

} // namespace hrz::vt
