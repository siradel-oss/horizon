#pragma once

#include "hrz/common/maths.h"
#include "hrz/core/palette_ubo.h"
#include "hrz/core/render/resources.h"

#include <lin_maths.h>

#include <stdint.h>

namespace hrz::model
{
enum
{
    MaterialCount = 2,
};

enum class DracoCompressionType : uint32_t
{
    None = 0,
    Quantized = 1,
    OctEncoded = 2,
};

struct VertexCompressionParamsUniformData
{
    lm::vec3 quantization_mins;
    DracoCompressionType type;
    lm::vec3 quantization_scale;
    uint32_t _padding[1];
};

HRZ_CHECK_UBO_SIZE(VertexCompressionParamsUniformData);

struct MeshGeometryUniformData
{
    lm::vec4 mesh_color;
    lm::uvec3 feature_reference;
    uint32_t object_id_offset;
    lm::uvec2 object_reference;
    bool32 overlay_material_enabled;
    float overlay_material_opacity;
    bool32 apply_feature_color_to_overlay;
    uint32_t feature_color_blend_mode;
    float feature_color_blend_strength;
    bool32 flat_shaded;
    bool32 draw_under_flat_overlays;
    int32_t clip_id;
    uint32_t _padding[2];
};

HRZ_CHECK_UBO_SIZE(MeshGeometryUniformData);

struct MeshUniformData
{
    HRZ_UBO_STRUCT_FIELD(MeshGeometryUniformData) geometry;
    HRZ_UBO_STRUCT_FIELD(PaletteUniformData) palettes[MaterialCount];
};

HRZ_CHECK_UBO_SIZE(MeshUniformData);

struct PrimitiveDrawGeometryUniformData
{
    HRZ_UBO_STRUCT_FIELD(VertexCompressionParamsUniformData) position_compression;
    HRZ_UBO_STRUCT_FIELD(VertexCompressionParamsUniformData) normal_compression;
    hrz::bool32 lighting_enabled;
    hrz::bool32 receive_shadows;
    uint32_t _padding[2];
};

HRZ_CHECK_UBO_SIZE(PrimitiveDrawGeometryUniformData);

struct PrimitiveDrawMaterialUniformData
{
    lm::vec4 material_color;
    uint32_t alpha_mode;
    float alpha_cutoff;
    bool32 use_data_texture;
    uint32_t _padding[1];
    HRZ_UBO_STRUCT_FIELD(VertexCompressionParamsUniformData) uv_compression;
};

HRZ_CHECK_UBO_SIZE(PrimitiveDrawMaterialUniformData);

struct PrimitiveDrawUniformData
{
    HRZ_UBO_STRUCT_FIELD(PrimitiveDrawGeometryUniformData) geometry;
    HRZ_UBO_STRUCT_FIELD(PrimitiveDrawMaterialUniformData) materials[MaterialCount];
};

HRZ_CHECK_UBO_SIZE(PrimitiveDrawUniformData);

struct PrimitiveTransformUniformData
{
    lm::mat4 transform;
    GlslStd140Mat3 normal_transform;
    lm::vec4 origin_low;
    lm::vec4 origin_high;
};

HRZ_CHECK_UBO_SIZE(PrimitiveTransformUniformData);

struct InstanceGroupUniformData
{
    GlslStd140Mat3 linear_transform;
    lm::vec4 origin_low;
    lm::vec4 origin_high;
    lm::uvec3 feature_reference;
    uint32_t object_id_offset;
    VertexCompressionParamsUniformData position_compression;
    VertexCompressionParamsUniformData normal_compression;
    lm::uvec2 object_reference;
};

HRZ_CHECK_UBO_SIZE(InstanceGroupUniformData);

struct ImpostorBakingUniformData
{
    lm::mat4 proj_matrix;
    lm::mat4 view_matrix;
};

HRZ_CHECK_UBO_SIZE(ImpostorBakingUniformData);

} // namespace hrz::model
