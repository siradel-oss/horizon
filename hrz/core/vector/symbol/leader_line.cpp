#include "hrz/core/vector/symbol/leader_line.h"

#include "hrz/common/color.h"
#include "hrz/common/fmt.h" // IWYU pragma: keep
#include "hrz/common/profiling.h"
#include "hrz/common/proto_maths.h"
#include "hrz/core/render/context.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/fnd/mem.h"

namespace hrz::vt::symbol
{
namespace
{
enum
{
    LeaderLineParamsUbo = ElementCustomUboStart,

    InMeshPosInputStream = 0,
    TargetPositionInputStream = 1,
    SymbolPositionInputStream = 2,
    ColorInputStream = 3,
    AnchorIndexInputStream = 4,
};

struct LeaderLineUniformData
{
    uint32_t z_index;
    float width;
    uint32_t _padding[2];
};

HRZ_CHECK_UBO_SIZE(LeaderLineUniformData);
} // namespace

void LeaderLineRenderable::render_callback(
    uint32_t render_type,
    my::RenderContext* r,
    my::ResourceBinder* rb,
    const void* user_data_raw,
    const void* raw_data)
{
    auto data = (const RenderData*)raw_data;
    const auto* user_data = (const hrz::SceneViewRenderGraphUserData*)user_data_raw;

    if (((1 << user_data->scene_view) & data->scene_views) == 0) return;

    my::ResourceHandle shader;
    switch (render_type)
    {
        case hrz::RenderVisual: shader = data->visual_shader; break;
        case hrz::RenderPicking: shader = data->picking_shader; break;
        case hrz::RenderSelection:
            if (!data->has_any_selected) return;
            shader = data->selection_shader;
            break;
        default: return;
    }

    auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, data->vertex_count)
                     .instanced(data->instance_count);

    rb->push_state();

    my::UboBinding ubo_bindings[] = {
        {TileParamsUbo, data->tile_ubo, 0, sizeof(TileUniformData)},
        {AnchorParamsUbo, data->anchor_ubo, 0, sizeof(AnchorUniformData)},
        {LeaderLineParamsUbo, data->leader_line_ubo, 0, sizeof(LeaderLineUniformData)},
    };
    rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

    my::TextureBinding texture_bindings[] = {
        {AnchorDataTextureSamplerIndex, data->anchor_data_texture, data->data_texture_sampler},
        {CullingVisibilitySamplerIndex, data->culling_visibility_textures[user_data->scene_view],
         data->data_texture_sampler},
        {SelectionSamplerIndex, data->selection_texture, data->data_texture_sampler},
    };
    rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

    auto state = rb->get_current_state();

    r->draw(
        batch, shader, data->vertex_input, state.ubo_count, state.ubos, state.texture_count,
        state.textures);

    rb->pop_state();
}

void LeaderLineElementSystem::collect_shaders(hrz::GpuResourceContext* rc)
{
    my::IndexName attribs[] = {
        {InMeshPosInputStream, "i_in_mesh_pos"},
        {TargetPositionInputStream, "i_target_in_tile_position"},
        {SymbolPositionInputStream, "i_symbol_position"},
        {ColorInputStream, "i_color"},
        {AnchorIndexInputStream, "i_anchor_index"},
    };

    static const my::IndexName ubos[] = {
        {hrz::UboFrame, "Frame"},
        {TileParamsUbo, "Tile"},
        {AnchorParamsUbo, "AnchorPrototype"},
        {LeaderLineParamsUbo, "LeaderLine"},
    };

    my::IndexName visual_samplers[] = {
        {AnchorDataTextureSamplerIndex, "u_anchors"},
        {hrz::SamplerCameraHeight, "u_camera_height"},
        {CullingVisibilitySamplerIndex, "u_visibility"},
    };

    static const char* outputs[] = {"o_color"};

    my::ShaderResource res{};
    res.name = hrz_shaders::Symbol_leader_line_name;
    res.vertex_source_len = hrz_shaders::Symbol_leader_line_vert_len;
    res.vertex_source = hrz_shaders::Symbol_leader_line_vert;
    res.fragment_source_len = hrz_shaders::Symbol_leader_line_frag_len;
    res.fragment_source = hrz_shaders::Symbol_leader_line_frag;
    res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
    res.uniform_blocks = ubos;
    res.output_count = HRZ_ARRAY_COUNT(outputs);
    res.outputs = outputs;
    res.attrib_count = HRZ_ARRAY_COUNT(attribs);
    res.attribs = attribs;
    res.sampler_count = HRZ_ARRAY_COUNT(visual_samplers);
    res.samplers = visual_samplers;

    res.initial_state.depth.test = true;
    res.initial_state.depth.compare = my::DepthState::Compare::LessEqual;

    res.initial_state.color_blend.enable = true;
    res.initial_state.color_blend.color.src = my::ColorBlendState::One;
    res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
    res.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
    res.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;
    res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
    rc->alloc(&res, hrz::monitoring::systems::Symbols);

    const char* picking_color_outputs[] = {"o_object_reference", "o_depth"};

    res.name = hrz_shaders::Symbol_leader_line_picking_name;
    res.vertex_source_len = hrz_shaders::Symbol_leader_line_picking_vert_len;
    res.vertex_source = hrz_shaders::Symbol_leader_line_picking_vert;
    res.fragment_source_len = hrz_shaders::Symbol_leader_line_picking_frag_len;
    res.fragment_source = hrz_shaders::Symbol_leader_line_picking_frag;
    res.output_count = HRZ_ARRAY_COUNT(picking_color_outputs);
    res.outputs = picking_color_outputs;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Symbols);

    static const char* selection_outputs[] = {"o_highlight"};

    my::IndexName selection_samplers[] = {
        {AnchorDataTextureSamplerIndex, "u_anchors"},
        {CullingVisibilitySamplerIndex, "u_visibility"},
        {SelectionSamplerIndex, "u_selection"},
        {hrz::SamplerCameraHeight, "u_camera_height"},
    };

    res.name = hrz_shaders::Symbol_leader_line_selection_name;
    res.vertex_source_len = hrz_shaders::Symbol_leader_line_selection_vert_len;
    res.vertex_source = hrz_shaders::Symbol_leader_line_selection_vert;
    res.fragment_source_len = hrz_shaders::Symbol_leader_line_selection_frag_len;
    res.fragment_source = hrz_shaders::Symbol_leader_line_selection_frag;
    res.output_count = HRZ_ARRAY_COUNT(selection_outputs);
    res.outputs = selection_outputs;
    res.sampler_count = HRZ_ARRAY_COUNT(selection_samplers);
    res.samplers = selection_samplers;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Symbols);
}

void LeaderLineElementSystem::init_render(Render* render)
{
    _visual_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_leader_line_name);
    _picking_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_leader_line_picking_name);
    _selection_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_leader_line_selection_name);

    {
        const lm::vec2 vertex_data[] = {{1, 0}, {1, 1}, {0, 0}, {0, 1}};

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = sizeof(vertex_data);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = (void*)&vertex_data[0].x;
        vb_res.allow_allocation_failure = false;

        _vertex_data_buffer = render->rc->alloc(
            &vb_res, hrz::monitoring::systems::Symbols,
            {{"contents"_ss, "leader line vertex data"_ss}});
    }
}

void LeaderLineElementSystem::deinit_render(Render* render)
{
    for (auto resource : _unused_resources)
    {
        render->rc->dealloc(resource);
    }
    _unused_resources.clear();

    render->rc->dealloc(_vertex_data_buffer);
}

ElementSystem::PrototypeH LeaderLineElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
        register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
{
    assert(element_descriptor.type() == ElementType);

    const auto& descriptor = element_descriptor.leader_line();

    Prototype prototype;
    prototype.layer_id = layer_id;
    prototype.z_index = z_index;

    prototype.baking_params.width = descriptor.width();

    prototype.baking_params.default_target_offset =
        hrz::to_lm(descriptor.target_offset().default_value());
    auto target_offset_x_prp_name = fmt::format("{}_x", descriptor.target_offset().name());
    auto target_offset_y_prp_name = fmt::format("{}_y", descriptor.target_offset().name());
    auto target_offset_z_prp_name = fmt::format("{}_z", descriptor.target_offset().name());
    prototype.baking_params.target_offset_prp.x =
        register_prp(target_offset_x_prp_name, prototype.baking_params.default_target_offset.x);
    prototype.baking_params.target_offset_prp.y =
        register_prp(target_offset_y_prp_name, prototype.baking_params.default_target_offset.y);
    prototype.baking_params.target_offset_prp.z =
        register_prp(target_offset_z_prp_name, prototype.baking_params.default_target_offset.z);

    prototype.baking_params.default_color_srgb =
        hrz::convert_proto_color_to_bytes(descriptor.color().default_value());
    auto color_prp_name = descriptor.color().name();
    prototype.baking_params.color_prp = register_prp(
        color_prp_name,
        vector_data::attr_from_color<vector_data::OwnedAttributeValue>(
            prototype.baking_params.default_color_srgb));

    prototype.status = Prototype::Status::Uploading;

    auto handle = _prototypes.alloc(std::move(prototype));

    _loading_prototypes.insert(handle);

    return {ElementType, handle};
}

void LeaderLineElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->baking_params.target_offset_prp.x);
        unregister_property(prototype->baking_params.target_offset_prp.y);
        unregister_property(prototype->baking_params.target_offset_prp.z);
        unregister_property(prototype->baking_params.color_prp);
    }
}

void LeaderLineElementSystem::delete_prototype(PrototypeH prototype_handle)
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        _loading_prototypes.erase(prototype_handle.handle);
        _deleted_prototypes.erase(prototype_handle.handle);
    }
}

ElementSystem::PrototypeStatus LeaderLineElementSystem::get_prototype_status(
    PrototypeH prototype_handle) const
{
    if (prototype_handle.type != ElementType)
    {
        return PrototypeStatus::Error;
    }

    auto prototype = _prototypes.get_object(prototype_handle.handle);

    if (prototype == nullptr)
    {
        return PrototypeStatus::Error;
    }

    switch (prototype->status)
    {
        case Prototype::Status::Uploading: return PrototypeStatus::Loading;
        case Prototype::Status::Ready: return PrototypeStatus::Ready;
        default: return PrototypeStatus::Error;
    }
}

std::optional<ElementSystem::RenderableH> LeaderLineElementSystem::make_renderable(
    PrototypeH prototype_handle,
    uint64_t layer_id,
    TileCoords tile_coords,
    hrz_jobs::BakedSymbols::ElementInstances&& baked_instances,
    double bsphere_radius,
    lm::dvec3 bsphere_center,
    my::ResourceHandle tile_ubo,
    my::ResourceHandle anchor_ubo,
    my::ResourceHandle anchor_data_texture,
    my::ResourceHandle selection_texture,
    const std::array<my::ResourceHandle, SCENE_VIEW_COUNT> culling_visibility_textures,
    my::ResourceHandle data_texture_sampler,
    uint32_t z_index,
    Render* render)
{
    if (prototype_handle.type != ElementType || baked_instances.type != ElementType)
    {
        assert(false && "Unexpected element type");
        HRZ_LOG_ERROR(
            "Unexpected element type: expect {}, got {}",
            hrz_proto::SymbolElementType_Name(ElementType),
            hrz_proto::SymbolElementType_Name(baked_instances.type));
        return std::nullopt;
    }

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype == nullptr)
    {
        HRZ_LOG_ERROR("Could not find prototype");
        return std::nullopt;
    }

    auto tile_coords_str = fmt::to_string(tile_coords);

    auto instance_blob = std::move(
        std::get<hrz::BlobArray<hrz_jobs::BakedSymbols::LeaderLineInstance>>(baked_instances.data));
    auto instance_data = instance_blob.get_data();

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = instance_data.size_bytes();
    vb_res.usage = my::UsageHint::Static;
    vb_res.data = (void*)instance_data.data();
    vb_res.allow_allocation_failure = true;

    auto instance_data_buffer = render->rc->alloc(
        &vb_res, hrz::monitoring::systems::Symbols, layer_id,
        {{"content"_ss, "leader line instance data"_ss}, {"tile coords"_ss, tile_coords_str}});

    using LeaderLineInstance = hrz_jobs::BakedSymbols::LeaderLineInstance;

    my::VertexInputStream streams[] = {
        {InMeshPosInputStream, _vertex_data_buffer, my::VertexFormat::Float32_2, 0,
         sizeof(lm::vec2), my::VertexRate::PerVertex},
        {TargetPositionInputStream, instance_data_buffer, my::VertexFormat::Float32_3,
         offsetof(LeaderLineInstance, target_in_tile_position), sizeof(LeaderLineInstance),
         my::VertexRate::PerInstance},
        {SymbolPositionInputStream, instance_data_buffer, my::VertexFormat::Float32_3,
         offsetof(LeaderLineInstance, in_symbol_position), sizeof(LeaderLineInstance),
         my::VertexRate::PerInstance},
        {ColorInputStream, instance_data_buffer, my::VertexFormat::UInt8Norm_4,
         offsetof(LeaderLineInstance, color), sizeof(LeaderLineInstance),
         my::VertexRate::PerInstance},
        {AnchorIndexInputStream, instance_data_buffer, my::VertexFormat::UInt32,
         offsetof(LeaderLineInstance, anchor_index), sizeof(LeaderLineInstance),
         my::VertexRate::PerInstance},
    };

    my::VertexInputResource vi_res;
    vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
    vi_res.attribs = streams;
    auto vertex_input = render->rc->alloc(
        &vi_res, hrz::monitoring::systems::Symbols, layer_id,
        {{"leader line vertex input"_ss, tile_coords_str}});

    LeaderLineRenderable renderable;
    renderable.center = bsphere_center;
    renderable.radius = bsphere_radius;
    renderable.z_index = z_index & ~0xff;

    renderable.vertex_data_buffer = _vertex_data_buffer;
    renderable.instance_data_buffer = instance_data_buffer;

    renderable.data.vertex_input = vertex_input;
    renderable.data.anchor_ubo = anchor_ubo;
    renderable.data.anchor_data_texture = anchor_data_texture;
    renderable.data.selection_texture = selection_texture;
    renderable.data.culling_visibility_textures = culling_visibility_textures;
    renderable.data.data_texture_sampler = data_texture_sampler;
    renderable.data.visual_shader = _visual_shader;
    renderable.data.picking_shader = _picking_shader;
    renderable.data.selection_shader = _selection_shader;
    renderable.data.tile_ubo = tile_ubo;
    renderable.data.leader_line_ubo = prototype->ubo;
    renderable.data.vertex_count = 4;
    renderable.data.instance_count = instance_data.size();

    return {{ElementType, _renderables.alloc(std::move(renderable))}};
}

void LeaderLineElementSystem::delete_renderable(RenderableH renderable_handle)
{
    if (renderable_handle.type != ElementType) return;

    auto renderable = _renderables.get_object(renderable_handle.handle);
    if (renderable)
    {
        _unused_resources.push_back(renderable->instance_data_buffer);
        _unused_resources.push_back(renderable->data.vertex_input);

        _renderables.release(renderable_handle.handle);
    }
}

void LeaderLineElementSystem::draw_renderable(
    RenderableH renderable_handle,
    uint32_t scene_views,
    bool has_any_selected,
    bool ignore_occlusions,
    Render* render)
{
    if (renderable_handle.type != ElementType) return;

    auto renderable = _renderables.get_object(renderable_handle.handle);
    if (renderable)
    {
        renderable->data.scene_views = scene_views;
        renderable->data.has_any_selected = has_any_selected;
        renderable->data.ignore_occlusions = ignore_occlusions;

        render->rd->collect_renderable(*renderable);
    }
}

void LeaderLineElementSystem::work(ReprSystem::WorkCtx& ctx)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol leader line work");

    for (auto handle : _deleted_prototypes)
    {
        auto prototype = _prototypes.get_object(handle);
        if (!prototype) continue;

        if (!prototype->ubo.is_null())
        {
            _unused_resources.push_back(prototype->ubo);
        }

        _prototypes.release(handle);
    }
    _deleted_prototypes.clear();
}

void LeaderLineElementSystem::work_gpu(Render* render)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol leader line work gpu");

    for (auto resource : _unused_resources)
    {
        render->rc->dealloc(resource);
    }
    _unused_resources.clear();

    for (auto prototype_handle : _loading_prototypes)
    {
        auto prototype = _prototypes.get_object(prototype_handle);
        if (!prototype) continue;

        assert(prototype->status == Prototype::Status::Uploading);

        LeaderLineUniformData ubo;
        ubo.z_index = prototype->z_index;
        ubo.width = prototype->baking_params.width;

        my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
        ub_res.size = sizeof(ubo);
        ub_res.usage = my::UsageHint::Static;
        ub_res.data = &ubo;
        prototype->ubo =
            render->rc->alloc(&ub_res, hrz::monitoring::systems::Symbols, prototype->layer_id);

        prototype->status =
            prototype->ubo.is_null() ? Prototype::Status::Error : Prototype::Status::Ready;
    }
    _loading_prototypes.clear();
}
} // namespace hrz::vt::symbol
