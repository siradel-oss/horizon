#pragma once

#include "hrz/common/shader_defines.h"

#include <mycelium/backend.h>

#include <cstdint>

#define HRZ_CHECK_UBO_SIZE(T)                                                          \
    static_assert(sizeof(T) % 16 == 0, "UBO " #T " should be a multiple of 16 bytes"); \
    static_assert(sizeof(T) <= 16384, "UBO " #T " exceeds maximum size of 16KB")

#define HRZ_UBO_STRUCT_FIELD(T) alignas(16) T

namespace hrz
{

enum CommonUbos
{
    UboFrame = 0,
    UboView,
    UboCustomStart,
};

enum CommonSamplers
{
    SamplerCameraHeight = 0,
    SamplerSunColor,
    SamplerSunShadow0,
    SamplerViewshedShadow0 = SamplerSunShadow0 + HRZ_S_MAX_SUN_CASCADES,
    SamplerCustomStart = SamplerViewshedShadow0 + HRZ_S_VIEWSHED_CNT,
};

using bool32 = uint32_t;

template<typename T>
constexpr size_t compute_ubo_stride(size_t ubo_alignment)
{
    HRZ_CHECK_UBO_SIZE(T);
    return ((sizeof(T) + ubo_alignment - 1) / ubo_alignment) * ubo_alignment;
}

namespace render
{

uint64_t acquire_texture_download_id();
void release_texture_download_id(uint64_t id);

void initialize_ui_blending_params(my::ColorBlendState*);

} // namespace render
} // namespace hrz
