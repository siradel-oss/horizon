#pragma once

#include "common/palette.glsl"

#define ALPHA_MODE_OPAQUE 0u
#define ALPHA_MODE_MASK 1u
#define ALPHA_MODE_BLEND 2u

#define COMPRESSION_NONE 0u
#define COMPRESSION_QUANTIZED 1u
#define COMPRESSION_OCT_ENCODED 2u

struct VertexCompressionParams
{
    vec3 quantization_mins;
    uint type;
    vec3 quantization_scale;
};

struct MeshGeometry
{
    vec4 mesh_color;
    uvec3 feature_reference;
    uint object_id_offset;
    uvec2 object_reference;
    bool overlay_material_enabled;
    float overlay_material_opacity;
    bool apply_feature_color_to_overlay;
    uint mesh_color_blend_mode;
    float mesh_color_blend_strength;
    bool flat_shaded;
    bool draw_under_flat_overlays;
    int clip_id;
};

layout(std140) uniform Mesh
{
    MeshGeometry geometry;
    Palette materials[2];
} hrz_mesh;

struct PrimitiveGeometry
{
    mat4 transform;
    mat3 normal_transform;
    vec4 origin_low;
    vec4 origin_high;
    VertexCompressionParams position_compression;
    VertexCompressionParams normal_compression;
    bool lighting_enabled;
    bool receive_shadows;
};

struct PrimitiveMaterial
{
    vec4 material_color;
    uint alpha_mode;
    float alpha_cutoff;
    bool use_data_texture;
    VertexCompressionParams uv_compression;
};

layout(std140) uniform Primitive
{
    PrimitiveGeometry geometry;
    PrimitiveMaterial materials[2];
} hrz_prim;

#ifdef GLTF_INSTANCED
layout(std140) uniform MeshInstanceGroup
{
    mat3 linear_transform;
    vec4 origin_low;
    vec4 origin_high;
    uvec3 feature_reference;
    uint object_id_offset;
    VertexCompressionParams position_compression;
    VertexCompressionParams normal_compression;
    uvec2 object_reference;
} hrz_instance_group;

const int DATA_TEXTURE_SIZE = HRZ_S_INSTANCE_GROUP_DATA_TEXTURE_WIDTH;
#endif

#ifdef GLTF_IMPOSTOR
layout(std140) uniform BakeFrame
{
    mat4 proj_matrix;
    mat4 view_matrix;
} hrz_impostor_bake_frame;
#endif
