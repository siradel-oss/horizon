#pragma once

#include "hrz/core/vector/flat_overlay.h"

#include <mycelium/backend.h>

namespace hrz::model
{
constexpr uint32_t CompressedStreamOffset = 1;
constexpr uint32_t UvStreamOffset = 2;

// @Note The indices of the compressed vertex streams are offset by
// CompressedStreamOffset based on their uncompressed counterpart. Make sure to
// not create overlaps when modifying those indices.
//      -slerouzic, 2021-11-29
enum
{
    PositionStreamIndex = 0,
    CompressedPositionStreamIndex = PositionStreamIndex + CompressedStreamOffset,

    NormalStreamIndex = 2,
    CompressedNormalStreamIndex = NormalStreamIndex + CompressedStreamOffset,

    ColorStreamIndex = 4,
    // Compressed colors don't exist

    Uv0StreamIndex = 6,
    CompressedUv0StreamIndex = Uv0StreamIndex + CompressedStreamOffset,

    Uv1StreamIndex = Uv0StreamIndex + UvStreamOffset,
    CompressedUv1StreamIndex = Uv1StreamIndex + CompressedStreamOffset,

    B3dm_BatchIdStreamIndex = 10
};

enum
{
    Material0Sampler = hrz::vector_flat_overlay::SamplerOverlayStart + HRZ_S_MAX_OVERLAY_CASCADES,
    Material1Sampler,

    _LastCommonSampler = Material1Sampler,

    B3dm_FeatureColorSampler = _LastCommonSampler + 1,
    B3dm_FeatureIdsSampler,
    B3dm_FeatureSelectionSampler,

    Instanced_PositionSampler = _LastCommonSampler + 1,
    Instanced_CompressedPositionSampler,
    Instanced_NormalSampler,
    Instanced_CompressedNormalSampler,
    Instanced_ScaleSampler,
    Instanced_ColorSampler,
    Instanced_ObjectIdSampler,
    Instanced_FeatureIdSampler,
    Instanced_SelectionSampler,
};

enum
{
    UboMeshParams = hrz::vector_flat_overlay::UboVectorOverlayCameras + 1,
    UboPrimitiveDrawParams,
    UboPrimitiveTransformParams,
    UboGroupParams,
    UboImpostorBaking,
};

struct SharedResources
{
    uint32_t ubo_alignment;

    size_t mesh_ubo_stride;
    size_t primitive_draw_ubo_stride;
    size_t primitive_transform_ubo_stride;

    my::ResourceHandle single_opaque_shader;
    my::ResourceHandle single_transparent_shader;
    my::ResourceHandle single_picking_shader;
    my::ResourceHandle single_depth_shader;
    my::ResourceHandle single_selection_shader;

    my::ResourceHandle impostor_baking_shader;

    my::ResourceHandle instanced_opaque_shader;
    my::ResourceHandle instanced_transparent_shader;
    my::ResourceHandle instanced_picking_shader;
    my::ResourceHandle instanced_depth_shader;
    my::ResourceHandle instanced_selection_shader;

    my::ResourceHandle b3dm_opaque_shader;
    my::ResourceHandle b3dm_opaque_float_batch_ids_shader;
    my::ResourceHandle b3dm_transparent_shader;
    my::ResourceHandle b3dm_transparent_float_batch_ids_shader;
    my::ResourceHandle b3dm_picking_shader;
    my::ResourceHandle b3dm_picking_float_batch_ids_shader;
    my::ResourceHandle b3dm_depth_shader;
    my::ResourceHandle b3dm_depth_float_batch_ids_shader;
    my::ResourceHandle b3dm_selection_shader;
    my::ResourceHandle b3dm_selection_float_batch_ids_shader;

    my::ResourceHandle fallback_position_vertex_buffer;
    my::ResourceHandle fallback_compressed_position_vertex_buffer;
    my::ResourceHandle fallback_normal_vertex_buffer;
    my::ResourceHandle fallback_compressed_normal_vertex_buffer;
    my::ResourceHandle fallback_color_vertex_buffer;
    my::ResourceHandle fallback_uv_vertex_buffer;
    my::ResourceHandle fallback_compressed_uv_vertex_buffer;
    my::ResourceHandle fallback_batch_id_vertex_buffer;
    my::VertexInputStream fallback_position_vertex_stream;
    my::VertexInputStream fallback_compressed_position_vertex_stream;
    my::VertexInputStream fallback_normal_vertex_stream;
    my::VertexInputStream fallback_compressed_normal_vertex_stream;
    my::VertexInputStream fallback_color_vertex_stream;
    my::VertexInputStream fallback_uv_vertex_stream;
    my::VertexInputStream fallback_compressed_uv_vertex_stream;
    my::VertexInputStream fallback_batch_id_vertex_stream;
    my::ResourceHandle fallback_texture;
    my::ResourceHandle fallback_color_sampler;
    my::ResourceHandle fallback_data_sampler;
};

} // namespace hrz::model
