#include "hrz/core/model/shared_resources.h"

#include "hrz/common/monitoring_defs.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/model/ubo_defs.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/core/shadows.h"
#include "hrz/core/sky.h"
#include "hrz/core/vector/flat_overlay.h"
#include "hrz/core/viewsheds.h"
#include "hrz/fnd/static_vector.h"

#include <mycelium/properties.h>

namespace hrz::model
{

// @Optimisation Only one array, with offsets, could be used.
template<typename T>
my::ResourceHandle _build_constant_vertex_array(
    const T& value,
    my::VertexFormat format,
    Render* render)
{
    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = sizeof(T);
    vb_res.usage = my::UsageHint::Static;
    vb_res.data = (void*)&value;

    return render->rc->alloc(&vb_res, hrz::monitoring::systems::Models);
}

my::VertexInputStream _build_constant_vertex_stream(
    my::ResourceHandle vertex_buffer,
    my::VertexFormat format,
    int index)
{
    my::VertexInputStream stream;
    stream.index = index;
    stream.buffer = vertex_buffer;
    stream.format = format;
    stream.offset = 0;
    stream.stride = my::vertex_size(format);
    stream.rate = my::VertexRate::Constant;

    return stream;
}

static void fetch_single_shaders(const Render* render, SharedResources* sr)
{
    sr->single_opaque_shader = render->rc->retrieve_shader(hrz_shaders::Gltf_name);
    sr->single_transparent_shader = render->rc->retrieve_shader("Gltf_transparent");
    sr->single_picking_shader = render->rc->retrieve_shader(hrz_shaders::Gltf_picking_name);
    sr->single_depth_shader = render->rc->retrieve_shader(hrz_shaders::Gltf_depth_name);
    sr->single_selection_shader = render->rc->retrieve_shader(hrz_shaders::Gltf_selection_name);
}

static void collect_single_shaders(hrz::GpuResourceContext* rc)
{
    my::IndexName attribs[] = {
        {PositionStreamIndex, "i_position"},
        {CompressedPositionStreamIndex, "i_compressed_position"},
        {NormalStreamIndex, "i_normal"},
        {CompressedNormalStreamIndex, "i_compressed_normal"},
        {ColorStreamIndex, "i_color"},
        {Uv0StreamIndex, "i_uv_0"},
        {CompressedUv0StreamIndex, "i_compressed_uv_0"},
        {Uv1StreamIndex, "i_uv_1"},
        {CompressedUv1StreamIndex, "i_compressed_uv_1"},
    };

    static const my::IndexName ubos[] = {
        {UboFrame, "Frame"},
        {UboMeshParams, "Mesh"},
        {UboPrimitiveDrawParams, "PrimitiveDraw"},
        {UboPrimitiveTransformParams, "PrimitiveTransform"},
    };

    static const my::IndexName ubos_depth[] = {
        {UboFrame, "Frame"},
        {UboView, "View"},
        {UboMeshParams, "Mesh"},
        {UboPrimitiveDrawParams, "PrimitiveDraw"},
        {UboPrimitiveTransformParams, "PrimitiveTransform"},
    };

    hrz::StaticVector<my::IndexName, 32> samplers_visual;
    samplers_visual.push_back({Material0Sampler, "hrz_material_texture_0"});
    samplers_visual.push_back({Material1Sampler, "hrz_material_texture_1"});

    if (get_flag(Flag::EnableShadows))
    {
        for (int i = 0; i < HRZ_S_MAX_SUN_CASCADES; ++i)
        {
            samplers_visual.push_back(
                {SamplerSunShadow0 + i, shadows::SUN_SHADOW_MAP_SAMPLER_NAMES[i]});
        }
    }

    if (get_flag(Flag::EnableAtmosphere))
    {
        samplers_visual.push_back({SamplerSunColor, sky::SUN_COLOR_SAMPLER_NAME});
    }

    for (int i = 0; i < HRZ_S_VIEWSHED_CNT; ++i)
    {
        samplers_visual.push_back(
            {SamplerViewshedShadow0 + i, viewsheds::VIEWSHED_SHADOW_MAP_SAMPLER_NAMES[i]});
    }

    my::IndexName samplers_non_visual[] = {
        {Material0Sampler, "hrz_material_texture_0"},
        {Material1Sampler, "hrz_material_texture_1"},
    };

    const char* color_outputs[] = {"o_color"};

    my::ShaderResource res{};
    res.name = hrz_shaders::Gltf_name;
    res.vertex_source_len = hrz_shaders::Gltf_vert_len;
    res.vertex_source = hrz_shaders::Gltf_vert;
    res.fragment_source_len = hrz_shaders::Gltf_frag_len;
    res.fragment_source = hrz_shaders::Gltf_frag;
    res.attribs = attribs;
    res.uniform_blocks = ubos;
    res.samplers = samplers_visual;
    res.outputs = color_outputs;
    res.initial_state.rasterization.cull_mode = my::RasterizationState::Back;
    res.initial_state.depth.test = true;
    res.initial_state.color_blend.enable = false;
    auto single_opaque_shader = rc->alloc(&res, hrz::monitoring::systems::Models);

    my::ShaderDerivativeResource res_d(single_opaque_shader, res, "Gltf_transparent");
    res_d.initial_state.color_blend.enable = true;
    res_d.initial_state.color_blend.color.src = my::ColorBlendState::One;
    res_d.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
    res_d.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
    res_d.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
    res_d.initial_state.rasterization.cull_mode = my::RasterizationState::None;
    rc->alloc(&res_d, hrz::monitoring::systems::Models);

    const char* picking_color_outputs[] = {"o_object_reference", "o_depth_value"};

    res.name = hrz_shaders::Gltf_picking_name;
    res.vertex_source_len = hrz_shaders::Gltf_picking_vert_len;
    res.vertex_source = hrz_shaders::Gltf_picking_vert;
    res.fragment_source_len = hrz_shaders::Gltf_picking_frag_len;
    res.fragment_source = hrz_shaders::Gltf_picking_frag;
    res.outputs = picking_color_outputs;
    res.samplers = samplers_non_visual;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Models);

    res.name = hrz_shaders::Gltf_depth_name;
    res.initial_state.color_blend.enable = false;
    res.vertex_source_len = hrz_shaders::Gltf_depth_vert_len;
    res.vertex_source = hrz_shaders::Gltf_depth_vert;
    res.fragment_source_len = hrz_shaders::Gltf_depth_frag_len;
    res.fragment_source = hrz_shaders::Gltf_depth_frag;
    res.outputs = {};
    res.uniform_blocks = ubos_depth;
    res.initial_state.rasterization.depth_bias_factor = 1.0F;
    res.initial_state.rasterization.depth_bias_units = 1.0F;
    rc->alloc(&res, hrz::monitoring::systems::Models);

    const char* selection_color_outputs[] = {"o_highlight"};

    res.name = hrz_shaders::Gltf_selection_name;
    res.initial_state.color_blend.enable = false;
    res.vertex_source_len = hrz_shaders::Gltf_selection_vert_len;
    res.vertex_source = hrz_shaders::Gltf_selection_vert;
    res.fragment_source_len = hrz_shaders::Gltf_selection_frag_len;
    res.fragment_source = hrz_shaders::Gltf_selection_frag;
    res.outputs = selection_color_outputs;
    res.samplers = samplers_non_visual;
    res.uniform_blocks = ubos;
    rc->alloc(&res, hrz::monitoring::systems::Models);
}

static void fetch_impostor_shaders(const Render* render, SharedResources* sr)
{
    sr->impostor_baking_shader = render->rc->retrieve_shader(hrz_shaders::GltfImpostorBaking_name);
}

static void collect_impostor_shaders(hrz::GpuResourceContext* rc)
{
    my::IndexName attribs[] = {
        {PositionStreamIndex, "i_position"},
        {CompressedPositionStreamIndex, "i_compressed_position"},
        {NormalStreamIndex, "i_normal"},
        {CompressedNormalStreamIndex, "i_compressed_normal"},
        {ColorStreamIndex, "i_color"},
        {Uv0StreamIndex, "i_uv_0"},
        {Uv1StreamIndex, "i_uv_1"},
        {CompressedUv0StreamIndex, "i_compressed_uv_0"},
        {CompressedUv1StreamIndex, "i_compressed_uv_1"},
    };

    static const my::IndexName ubos[] = {
        {UboMeshParams, "Mesh"},
        {UboPrimitiveDrawParams, "PrimitiveDraw"},
        {UboPrimitiveTransformParams, "PrimitiveTransform"},
        {UboImpostorBaking, "BakeFrame"},
    };

    my::IndexName samplers[] = {
        {Material0Sampler, "hrz_material_texture_0"},
    };

    const char* color_outputs[] = {"o_color", "o_normal"};

    my::ShaderResource res{};
    res.name = hrz_shaders::GltfImpostorBaking_name;
    res.vertex_source_len = hrz_shaders::GltfImpostorBaking_vert_len;
    res.vertex_source = hrz_shaders::GltfImpostorBaking_vert;
    res.fragment_source_len = hrz_shaders::GltfImpostorBaking_frag_len;
    res.fragment_source = hrz_shaders::GltfImpostorBaking_frag;
    res.attribs = attribs;
    res.uniform_blocks = ubos;
    res.samplers = samplers;
    res.outputs = color_outputs;

    res.initial_state.rasterization.cull_mode = my::RasterizationState::Back;
    res.initial_state.depth.test = true;
    res.initial_state.color_blend.enable = false;

    // When impostors are being rendered, we cannot afford to wait even one
    // frame before the shader is ready, as that would result in missing
    // entries in the atlas. So if the shader isn't already linked when used,
    // it needs to be linked on the spot.
    res.link_hint = my::ShaderLinkHint::FirstUseImmediate;

    rc->alloc(&res, hrz::monitoring::systems::Models);
}

static void fetch_instanced_shaders(const Render* render, SharedResources* sr)
{
    sr->instanced_opaque_shader = render->rc->retrieve_shader(hrz_shaders::GltfInstanced_name);
    sr->instanced_transparent_shader = render->rc->retrieve_shader("GltfInstanced_transparent");
    sr->instanced_picking_shader =
        render->rc->retrieve_shader(hrz_shaders::GltfInstanced_picking_name);
    sr->instanced_depth_shader = render->rc->retrieve_shader(hrz_shaders::GltfInstanced_depth_name);
    sr->instanced_selection_shader =
        render->rc->retrieve_shader(hrz_shaders::GltfInstanced_selection_name);
}

static void collect_instanced_shaders(hrz::GpuResourceContext* rc)
{
    my::IndexName attribs[] = {
        {PositionStreamIndex, "i_position"},
        {CompressedPositionStreamIndex, "i_compressed_position"},
        {NormalStreamIndex, "i_normal"},
        {CompressedNormalStreamIndex, "i_compressed_normal"},
        {ColorStreamIndex, "i_color"},
        {Uv0StreamIndex, "i_uv_0"},
        {CompressedUv0StreamIndex, "i_compressed_uv_0"},
        {Uv1StreamIndex, "i_uv_1"},
        {CompressedUv1StreamIndex, "i_compressed_uv_1"},
    };

    static const my::IndexName ubos[] = {
        {UboFrame, "Frame"},
        {UboMeshParams, "Mesh"},
        {UboPrimitiveDrawParams, "PrimitiveDraw"},
        {UboPrimitiveTransformParams, "PrimitiveTransform"},
        {UboGroupParams, "MeshInstanceGroup"},
    };

    static const my::IndexName ubos_depth[] = {
        {UboFrame, "Frame"},
        {UboView, "View"},
        {UboMeshParams, "Mesh"},
        {UboPrimitiveDrawParams, "PrimitiveDraw"},
        {UboPrimitiveTransformParams, "PrimitiveTransform"},
        {UboGroupParams, "MeshInstanceGroup"},
    };

    hrz::StaticVector<my::IndexName, 32> samplers_visual;
    samplers_visual.push_back({Material0Sampler, "hrz_material_texture_0"});
    samplers_visual.push_back({Material1Sampler, "hrz_material_texture_1"});
    samplers_visual.push_back({Instanced_PositionSampler, "u_instance_position"});
    samplers_visual.push_back(
        {Instanced_CompressedPositionSampler, "u_instance_compressed_position"});
    samplers_visual.push_back({Instanced_NormalSampler, "u_instance_normal"});
    samplers_visual.push_back({Instanced_CompressedNormalSampler, "u_instance_compressed_normal"});
    samplers_visual.push_back({Instanced_ScaleSampler, "u_instance_scale"});
    samplers_visual.push_back({Instanced_ColorSampler, "u_instance_color"});
    samplers_visual.push_back({Instanced_FeatureIdSampler, "u_instance_feature_id"});

    if (get_flag(Flag::EnableShadows))
    {
        for (int i = 0; i < HRZ_S_MAX_SUN_CASCADES; ++i)
        {
            samplers_visual.push_back(
                {hrz::SamplerSunShadow0 + i, hrz::shadows::SUN_SHADOW_MAP_SAMPLER_NAMES[i]});
        }
    }

    if (get_flag(Flag::EnableAtmosphere))
    {
        samplers_visual.push_back({hrz::SamplerSunColor, hrz::sky::SUN_COLOR_SAMPLER_NAME});
    }

    for (int i = 0; i < HRZ_S_VIEWSHED_CNT; ++i)
    {
        samplers_visual.push_back(
            {hrz::SamplerViewshedShadow0 + i,
             hrz::viewsheds::VIEWSHED_SHADOW_MAP_SAMPLER_NAMES[i]});
    }

    my::IndexName samplers_picking[] = {
        {Material0Sampler, "hrz_material_texture_0"},
        {Material1Sampler, "hrz_material_texture_1"},
        {Instanced_PositionSampler, "u_instance_position"},
        {Instanced_CompressedPositionSampler, "u_instance_compressed_position"},
        {Instanced_NormalSampler, "u_instance_normal"},
        {Instanced_CompressedNormalSampler, "u_instance_compressed_normal"},
        {Instanced_ScaleSampler, "u_instance_scale"},
        {Instanced_ColorSampler, "u_instance_color"},
        {Instanced_ObjectIdSampler, "u_instance_object_id"},
    };

    my::IndexName samplers_depth[] = {
        {Material0Sampler, "hrz_material_texture_0"},
        {Material1Sampler, "hrz_material_texture_1"},
        {Instanced_PositionSampler, "u_instance_position"},
        {Instanced_CompressedPositionSampler, "u_instance_compressed_position"},
        {Instanced_NormalSampler, "u_instance_normal"},
        {Instanced_CompressedNormalSampler, "u_instance_compressed_normal"},
        {Instanced_ScaleSampler, "u_instance_scale"},
        {Instanced_ColorSampler, "u_instance_color"},
    };

    my::IndexName samplers_selection[] = {
        {Material0Sampler, "hrz_material_texture_0"},
        {Material1Sampler, "hrz_material_texture_1"},
        {Instanced_PositionSampler, "u_instance_position"},
        {Instanced_CompressedPositionSampler, "u_instance_compressed_position"},
        {Instanced_NormalSampler, "u_instance_normal"},
        {Instanced_CompressedNormalSampler, "u_instance_compressed_normal"},
        {Instanced_ScaleSampler, "u_instance_scale"},
        {Instanced_ColorSampler, "u_instance_color"},
        {Instanced_SelectionSampler, "u_selection"},
    };

    const char* color_outputs[] = {"o_color"};

    my::ShaderResource res{};
    res.name = hrz_shaders::GltfInstanced_name;
    res.vertex_source_len = hrz_shaders::GltfInstanced_vert_len;
    res.vertex_source = hrz_shaders::GltfInstanced_vert;
    res.fragment_source_len = hrz_shaders::GltfInstanced_frag_len;
    res.fragment_source = hrz_shaders::GltfInstanced_frag;
    res.attribs = attribs;
    res.uniform_blocks = ubos;
    res.samplers = samplers_visual;
    res.outputs = color_outputs;
    res.initial_state.rasterization.cull_mode = my::RasterizationState::Back;
    res.initial_state.depth.test = true;
    res.initial_state.color_blend.enable = false;
    auto instanced_opaque_shader = rc->alloc(&res, hrz::monitoring::systems::Models);

    my::ShaderDerivativeResource res_d(instanced_opaque_shader, res, "GltfInstanced_transparent");
    res_d.initial_state.color_blend.enable = true;
    res_d.initial_state.color_blend.color.src = my::ColorBlendState::One;
    res_d.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
    res_d.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
    res_d.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
    rc->alloc(&res_d, hrz::monitoring::systems::Models);

    const char* picking_color_outputs[] = {"o_object_reference", "o_depth_value"};

    res.name = hrz_shaders::GltfInstanced_picking_name;
    res.vertex_source_len = hrz_shaders::GltfInstanced_picking_vert_len;
    res.vertex_source = hrz_shaders::GltfInstanced_picking_vert;
    res.fragment_source_len = hrz_shaders::GltfInstanced_picking_frag_len;
    res.fragment_source = hrz_shaders::GltfInstanced_picking_frag;
    res.samplers = samplers_picking;
    res.outputs = picking_color_outputs;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Models);

    const char* selection_color_outputs[] = {"o_highlight"};

    res.name = hrz_shaders::GltfInstanced_selection_name;
    res.vertex_source_len = hrz_shaders::GltfInstanced_selection_vert_len;
    res.vertex_source = hrz_shaders::GltfInstanced_selection_vert;
    res.fragment_source_len = hrz_shaders::GltfInstanced_selection_frag_len;
    res.fragment_source = hrz_shaders::GltfInstanced_selection_frag;
    res.samplers = samplers_selection;
    res.outputs = selection_color_outputs;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Models);

    res.name = hrz_shaders::GltfInstanced_depth_name;
    res.vertex_source_len = hrz_shaders::GltfInstanced_depth_vert_len;
    res.vertex_source = hrz_shaders::GltfInstanced_depth_vert;
    res.fragment_source_len = hrz_shaders::GltfInstanced_depth_frag_len;
    res.fragment_source = hrz_shaders::GltfInstanced_depth_frag;
    res.uniform_blocks = ubos_depth;
    res.samplers = samplers_depth;
    res.outputs = {};
    res.initial_state.rasterization.depth_bias_factor = 1.0F;
    res.initial_state.rasterization.depth_bias_units = 1.0F;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Models);
}

static void fetch_batched_shaders(const Render* render, SharedResources* sr)
{
    sr->b3dm_opaque_shader = render->rc->retrieve_shader(hrz_shaders::ThreeDTilesB3dm_name);
    sr->b3dm_transparent_shader = render->rc->retrieve_shader("ThreeDTilesB3dm_transparent");
    sr->b3dm_picking_shader =
        render->rc->retrieve_shader(hrz_shaders::ThreeDTilesB3dm_picking_name);
    sr->b3dm_selection_shader =
        render->rc->retrieve_shader(hrz_shaders::ThreeDTilesB3dm_selection_name);
    sr->b3dm_depth_shader = render->rc->retrieve_shader(hrz_shaders::ThreeDTilesB3dm_depth_name);

    sr->b3dm_opaque_float_batch_ids_shader =
        render->rc->retrieve_shader(hrz_shaders::ThreeDTilesB3dmFloatBatchIds_name);
    sr->b3dm_transparent_float_batch_ids_shader =
        render->rc->retrieve_shader("ThreeDTilesB3dmFloatBatchIds_transparent");
    sr->b3dm_picking_float_batch_ids_shader =
        render->rc->retrieve_shader(hrz_shaders::ThreeDTilesB3dmFloatBatchIds_picking_name);
    sr->b3dm_selection_float_batch_ids_shader =
        render->rc->retrieve_shader(hrz_shaders::ThreeDTilesB3dmFloatBatchIds_selection_name);
    sr->b3dm_depth_float_batch_ids_shader =
        render->rc->retrieve_shader(hrz_shaders::ThreeDTilesB3dmFloatBatchIds_depth_name);
}

static void collect_batched_shaders(hrz::GpuResourceContext* rc)
{
    my::IndexName attribs[] = {
        {PositionStreamIndex, "i_position"},
        {CompressedPositionStreamIndex, "i_compressed_position"},
        {NormalStreamIndex, "i_normal"},
        {CompressedNormalStreamIndex, "i_compressed_normal"},
        {ColorStreamIndex, "i_color"},
        {Uv0StreamIndex, "i_uv_0"},
        {CompressedUv0StreamIndex, "i_compressed_uv_0"},
        {Uv1StreamIndex, "i_uv_1"},
        {CompressedUv1StreamIndex, "i_compressed_uv_1"},
        {B3dm_BatchIdStreamIndex, "i_batch_id"}
    };

    static const my::IndexName ubos[] = {
        {hrz::UboFrame, "Frame"},
        {hrz::vector_flat_overlay::UboVectorOverlayCameras, "OverlayCamerasUniform"},
        {UboMeshParams, "Mesh"},
        {UboPrimitiveDrawParams, "PrimitiveDraw"},
        {UboPrimitiveTransformParams, "PrimitiveTransform"},
    };

    static const my::IndexName ubos_depth[] = {
        {UboFrame, "Frame"},
        {UboView, "View"},
        {UboMeshParams, "Mesh"},
        {UboPrimitiveDrawParams, "PrimitiveDraw"},
        {UboPrimitiveTransformParams, "PrimitiveTransform"},
    };

    hrz::StaticVector<my::IndexName, 32> visual_samplers;
    visual_samplers.push_back({Material0Sampler, "hrz_material_texture_0"});
    visual_samplers.push_back({Material1Sampler, "hrz_material_texture_1"});
    visual_samplers.push_back({B3dm_FeatureColorSampler, "u_color_attribute_texture"});
    visual_samplers.push_back({B3dm_FeatureIdsSampler, "u_feature_ids_texture"});

    for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
    {
        visual_samplers.push_back(
            {hrz::vector_flat_overlay::SamplerOverlayStart + i,
             hrz::vector_flat_overlay::sampler_names[i]});
    }

    if (get_flag(Flag::EnableShadows))
    {
        for (int i = 0; i < HRZ_S_MAX_SUN_CASCADES; ++i)
        {
            visual_samplers.push_back(
                {hrz::SamplerSunShadow0 + i, hrz::shadows::SUN_SHADOW_MAP_SAMPLER_NAMES[i]});
        }
    }

    if (get_flag(Flag::EnableAtmosphere))
    {
        visual_samplers.push_back({hrz::SamplerSunColor, hrz::sky::SUN_COLOR_SAMPLER_NAME});
    }

    for (int i = 0; i < HRZ_S_VIEWSHED_CNT; ++i)
    {
        visual_samplers.push_back(
            {hrz::SamplerViewshedShadow0 + i,
             hrz::viewsheds::VIEWSHED_SHADOW_MAP_SAMPLER_NAMES[i]});
    }

    hrz::StaticVector<my::IndexName, 32> picking_samplers;
    picking_samplers.push_back({Material0Sampler, "hrz_material_texture_0"});
    picking_samplers.push_back({Material1Sampler, "hrz_material_texture_1"});
    picking_samplers.push_back({B3dm_FeatureColorSampler, "u_color_attribute_texture"});

    for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
    {
        picking_samplers.push_back(
            {hrz::vector_flat_overlay::SamplerOverlayStart + i,
             hrz::vector_flat_overlay::picking_sampler_names[i]});
    }

    hrz::StaticVector<my::IndexName, 32> selection_samplers;
    selection_samplers.push_back({Material0Sampler, "hrz_material_texture_0"});
    selection_samplers.push_back({Material1Sampler, "hrz_material_texture_1"});
    selection_samplers.push_back({B3dm_FeatureColorSampler, "u_color_attribute_texture"});
    selection_samplers.push_back({B3dm_FeatureSelectionSampler, "u_selection_texture"});

    for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
    {
        selection_samplers.push_back(
            {hrz::vector_flat_overlay::SamplerOverlayStart + i,
             hrz::vector_flat_overlay::selection_sampler_names[i]});
    }

    hrz::StaticVector<my::IndexName, 32> depth_samplers;
    depth_samplers.push_back({Material0Sampler, "hrz_material_texture_0"});
    depth_samplers.push_back({Material1Sampler, "hrz_material_texture_1"});
    depth_samplers.push_back({B3dm_FeatureColorSampler, "u_color_attribute_texture"});

    const char* color_outputs[] = {"o_color"};

    my::ShaderResource res{};
    res.name = hrz_shaders::ThreeDTilesB3dm_name;
    res.vertex_source_len = hrz_shaders::ThreeDTilesB3dm_vert_len;
    res.vertex_source = hrz_shaders::ThreeDTilesB3dm_vert;
    res.fragment_source_len = hrz_shaders::ThreeDTilesB3dm_frag_len;
    res.fragment_source = hrz_shaders::ThreeDTilesB3dm_frag;
    res.attribs = attribs;
    res.uniform_blocks = ubos;
    res.samplers = visual_samplers;
    res.outputs = color_outputs;
    res.initial_state.rasterization.cull_mode = my::RasterizationState::Back;
    res.initial_state.depth.test = true;
    res.initial_state.color_blend.enable = false;
    auto b3dm_opaque_shader = rc->alloc(&res, hrz::monitoring::systems::Models);

    my::ShaderResource res_f = res;
    res_f.name = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_name;
    res_f.vertex_source_len = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_vert_len;
    res_f.vertex_source = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_vert;
    res_f.fragment_source_len = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_frag_len;
    res_f.fragment_source = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_frag;
    res_f.initial_state.rasterization.cull_mode = my::RasterizationState::Back;
    auto b3dm_opaque_float_batch_ids_shader = rc->alloc(&res_f, hrz::monitoring::systems::Models);

    my::ShaderDerivativeResource res_d(b3dm_opaque_shader, res, "ThreeDTilesB3dm_transparent");
    res_d.initial_state.color_blend.enable = true;
    res_d.initial_state.color_blend.color.src = my::ColorBlendState::One;
    res_d.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
    res_d.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
    res_d.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
    rc->alloc(&res_d, hrz::monitoring::systems::Models);

    my::ShaderDerivativeResource res_f_d(
        b3dm_opaque_float_batch_ids_shader, res_f, "ThreeDTilesB3dmFloatBatchIds_transparent");
    res_f_d.initial_state.color_blend.enable = true;
    res_f_d.initial_state.color_blend.color.src = my::ColorBlendState::One;
    res_f_d.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
    res_f_d.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
    res_f_d.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
    rc->alloc(&res_f_d, hrz::monitoring::systems::Models);

    const char* picking_color_outputs[] = {"o_object_reference", "o_depth_value"};

    res.name = hrz_shaders::ThreeDTilesB3dm_picking_name;
    res.vertex_source_len = hrz_shaders::ThreeDTilesB3dm_picking_vert_len;
    res.vertex_source = hrz_shaders::ThreeDTilesB3dm_picking_vert;
    res.fragment_source_len = hrz_shaders::ThreeDTilesB3dm_picking_frag_len;
    res.fragment_source = hrz_shaders::ThreeDTilesB3dm_picking_frag;
    res.samplers = picking_samplers;
    res.outputs = picking_color_outputs;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Models);

    res_f = res;
    res_f.name = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_picking_name;
    res_f.vertex_source_len = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_picking_vert_len;
    res_f.vertex_source = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_picking_vert;
    res_f.fragment_source_len = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_picking_frag_len;
    res_f.fragment_source = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_picking_frag;
    rc->alloc(&res_f, hrz::monitoring::systems::Models);

    const char* selection_color_outputs[] = {"o_highlight"};

    res.name = hrz_shaders::ThreeDTilesB3dm_selection_name;
    res.vertex_source_len = hrz_shaders::ThreeDTilesB3dm_selection_vert_len;
    res.vertex_source = hrz_shaders::ThreeDTilesB3dm_selection_vert;
    res.fragment_source_len = hrz_shaders::ThreeDTilesB3dm_selection_frag_len;
    res.fragment_source = hrz_shaders::ThreeDTilesB3dm_selection_frag;
    res.samplers = selection_samplers;
    res.outputs = selection_color_outputs;
    rc->alloc(&res, hrz::monitoring::systems::Models);

    res_f = res;
    res_f.name = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_selection_name;
    res_f.vertex_source_len = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_selection_vert_len;
    res_f.vertex_source = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_selection_vert;
    res_f.fragment_source_len = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_selection_frag_len;
    res_f.fragment_source = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_selection_frag;
    rc->alloc(&res_f, hrz::monitoring::systems::Models);

    res.name = hrz_shaders::ThreeDTilesB3dm_depth_name;
    res.vertex_source_len = hrz_shaders::ThreeDTilesB3dm_depth_vert_len;
    res.vertex_source = hrz_shaders::ThreeDTilesB3dm_depth_vert;
    res.fragment_source_len = hrz_shaders::ThreeDTilesB3dm_depth_frag_len;
    res.fragment_source = hrz_shaders::ThreeDTilesB3dm_depth_frag;
    res.outputs = {};
    res.samplers = depth_samplers;
    res.uniform_blocks = ubos_depth;
    res.initial_state.rasterization.depth_bias_factor = 1.0F;
    res.initial_state.rasterization.depth_bias_units = 1.0F;
    rc->alloc(&res, hrz::monitoring::systems::Models);

    res_f = res;
    res_f.name = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_depth_name;
    res_f.vertex_source_len = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_depth_vert_len;
    res_f.vertex_source = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_depth_vert;
    res_f.fragment_source_len = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_depth_frag_len;
    res_f.fragment_source = hrz_shaders::ThreeDTilesB3dmFloatBatchIds_depth_frag;
    rc->alloc(&res_f, hrz::monitoring::systems::Models);
}

static SharedResources* create_shared_resources_common(Render* render)
{
    SharedResources* sr = new SharedResources();

    sr->ubo_alignment = (uint32_t)render->my->get_uniform_buffer_offset_alignment();

    sr->mesh_ubo_stride = compute_ubo_stride<MeshUniformData>(sr->ubo_alignment);
    sr->primitive_draw_ubo_stride = compute_ubo_stride<PrimitiveDrawUniformData>(sr->ubo_alignment);
    sr->primitive_transform_ubo_stride =
        compute_ubo_stride<PrimitiveTransformUniformData>(sr->ubo_alignment);

    sr->fallback_position_vertex_buffer =
        _build_constant_vertex_array<lm::vec3>({0, 0, 0}, my::VertexFormat::Float32_3, render);
    sr->fallback_compressed_position_vertex_buffer =
        _build_constant_vertex_array<lm::uvec3>({0, 0, 0}, my::VertexFormat::UInt32_3, render);
    sr->fallback_normal_vertex_buffer =
        _build_constant_vertex_array<lm::vec3>({0, 0, 1}, my::VertexFormat::Float32_3, render);
    sr->fallback_compressed_normal_vertex_buffer =
        _build_constant_vertex_array<lm::uvec3>({0, 0, 1}, my::VertexFormat::UInt32_3, render);
    sr->fallback_color_vertex_buffer =
        _build_constant_vertex_array<lm::vec4>({1, 1, 1, 1}, my::VertexFormat::Float32_4, render);
    sr->fallback_uv_vertex_buffer =
        _build_constant_vertex_array<lm::vec2>({0, 0}, my::VertexFormat::Float32_2, render);
    sr->fallback_compressed_uv_vertex_buffer =
        _build_constant_vertex_array<lm::uvec2>({0, 0}, my::VertexFormat::UInt32_2, render);
    sr->fallback_batch_id_vertex_buffer =
        _build_constant_vertex_array<lm::vec2>({0, 0}, my::VertexFormat::UInt8, render);

    sr->fallback_position_vertex_stream = _build_constant_vertex_stream(
        sr->fallback_position_vertex_buffer, my::VertexFormat::Float32_3, PositionStreamIndex);
    sr->fallback_compressed_position_vertex_stream = _build_constant_vertex_stream(
        sr->fallback_compressed_position_vertex_buffer, my::VertexFormat::UInt32_3,
        CompressedPositionStreamIndex);
    sr->fallback_normal_vertex_stream = _build_constant_vertex_stream(
        sr->fallback_normal_vertex_buffer, my::VertexFormat::Float32_3, NormalStreamIndex);
    sr->fallback_compressed_normal_vertex_stream = _build_constant_vertex_stream(
        sr->fallback_compressed_normal_vertex_buffer, my::VertexFormat::UInt32_3,
        CompressedNormalStreamIndex);
    sr->fallback_color_vertex_stream = _build_constant_vertex_stream(
        sr->fallback_color_vertex_buffer, my::VertexFormat::Float32_4, ColorStreamIndex);
    sr->fallback_uv_vertex_stream = _build_constant_vertex_stream(
        sr->fallback_uv_vertex_buffer, my::VertexFormat::Float32_2, Uv0StreamIndex);
    sr->fallback_compressed_uv_vertex_stream = _build_constant_vertex_stream(
        sr->fallback_compressed_uv_vertex_buffer, my::VertexFormat::UInt32_2,
        CompressedUv0StreamIndex);
    sr->fallback_batch_id_vertex_stream = _build_constant_vertex_stream(
        sr->fallback_batch_id_vertex_buffer, my::VertexFormat::UInt8, B3dm_BatchIdStreamIndex);

    {
        uint8_t image_data[] = {0xff, 0xff, 0xff, 0xff};
        std::span<const std::byte> res_data = {(const std::byte*)image_data, sizeof(uint8_t) * 4};

        my::TextureResource res;
        res.layout.type = my::TextureLayout::Type2D;
        res.layout.format = my::TextureFormat::RGBA8;
        res.layout.width = 1;
        res.layout.height = 1;
        res.layout.depth = 1;
        res.layout.levels = 1;
        res.data = {&res_data, 1};
        res.generate_mipmaps = false;

        sr->fallback_texture = render->rc->alloc(
            &res, monitoring::systems::Models, {{"contents"_ss, "model fallback texture"_ss}});
    }

    {
        my::SamplerResource sampler_res;
        sampler_res.sampler.min_filter = my::SamplerParams::Filter::Linear;
        sampler_res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
        sampler_res.sampler.mipmap_filter = my::SamplerParams::Filter::Linear;
        sampler_res.sampler.wrap_x = my::SamplerParams::Wrap::Repeat;
        sampler_res.sampler.wrap_y = my::SamplerParams::Wrap::Repeat;
        sampler_res.sampler.wrap_z = my::SamplerParams::Wrap::Repeat;
        sampler_res.use_mipmaps = true;

        sr->fallback_color_sampler =
            render->rc->alloc(&sampler_res, hrz::monitoring::systems::Models);
    }

    {
        my::SamplerResource sampler_res;
        sampler_res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
        sampler_res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
        sampler_res.sampler.mipmap_filter = my::SamplerParams::Filter::Nearest;
        sampler_res.sampler.wrap_x = my::SamplerParams::Wrap::Repeat;
        sampler_res.sampler.wrap_y = my::SamplerParams::Wrap::Repeat;
        sampler_res.sampler.wrap_z = my::SamplerParams::Wrap::Repeat;
        sampler_res.use_mipmaps = false;

        sr->fallback_data_sampler =
            render->rc->alloc(&sampler_res, hrz::monitoring::systems::Models);
    }

    return sr;
}

SharedResources* create_shared_resources_single(Render* render)
{
    auto* sr = create_shared_resources_common(render);
    fetch_single_shaders(render, sr);
    return sr;
}

SharedResources* create_shared_resources_instanced(Render* render)
{
    auto* sr = create_shared_resources_common(render);
    fetch_instanced_shaders(render, sr);
    fetch_impostor_shaders(render, sr);
    return sr;
}

SharedResources* create_shared_resources_batched(Render* render)
{
    auto* sr = create_shared_resources_common(render);
    fetch_batched_shaders(render, sr);
    return sr;
}

void destroy_shared_resources(SharedResources* sr, Render* render)
{
    render->rc->dealloc(sr->fallback_position_vertex_buffer);
    render->rc->dealloc(sr->fallback_compressed_position_vertex_buffer);
    render->rc->dealloc(sr->fallback_normal_vertex_buffer);
    render->rc->dealloc(sr->fallback_compressed_normal_vertex_buffer);
    render->rc->dealloc(sr->fallback_color_vertex_buffer);
    render->rc->dealloc(sr->fallback_uv_vertex_buffer);
    render->rc->dealloc(sr->fallback_compressed_uv_vertex_buffer);
    render->rc->dealloc(sr->fallback_batch_id_vertex_buffer);

    render->rc->dealloc(sr->fallback_texture);
    render->rc->dealloc(sr->fallback_data_sampler);
    render->rc->dealloc(sr->fallback_color_sampler);

    delete sr;
}

void collect_shaders(hrz::GpuResourceContext* rc)
{
    collect_single_shaders(rc);
    collect_impostor_shaders(rc);
    collect_instanced_shaders(rc);
    collect_batched_shaders(rc);
}

} // namespace hrz::model
