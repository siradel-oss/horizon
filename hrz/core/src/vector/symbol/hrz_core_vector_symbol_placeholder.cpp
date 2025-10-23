#include "vector/symbol/hrz_core_vector_symbol_placeholder.h"

#include "hrz_core_shaders.h"

#include <hrz_common_color.h>
#include <hrz_common_fmt.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_mem.h>

namespace hrz::vt::symbol
{
namespace
{
enum
{
    PlaceholderParamsUbo = ElementCustomUboStart,

    InMeshPosInputStream = 0,
    TransformInputStream = 1,
    SizeInputStream = 5,
    ColorInputStream = 6,
    AnchorIndexInputStream = 7,
};

struct PlaceholderUniformData
{
    uint32_t z_index;
    uint32_t _padding[3];
};

HRZ_CHECK_UBO_SIZE(PlaceholderUniformData);
} // namespace

void PlaceholderRenderable::render_callback(
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
        {PlaceholderParamsUbo, data->placeholder_ubo, 0, sizeof(PlaceholderUniformData)},
    };
    rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

    my::TextureBinding texture_bindings[] = {
        {AnchorDataTextureSamplerIndex, data->anchor_data_texture, data->data_texture_sampler},
        {SelectionSamplerIndex, data->selection_texture, data->data_texture_sampler},
        {CullingVisibilitySamplerIndex, data->culling_visibility_textures[user_data->scene_view],
         data->data_texture_sampler},
    };
    rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

    auto state = rb->get_current_state();

    r->draw(
        batch, shader, data->vertex_input, state.ubo_count, state.ubos, state.texture_count,
        state.textures);

    rb->pop_state();
}

void PlaceholderElementSystem::collect_shaders(hrz::GpuResourceContext* rc)
{
    my::IndexName attribs[] = {
        {InMeshPosInputStream, "i_in_mesh_pos"},
        {TransformInputStream, "i_transform"},
        {SizeInputStream, "i_size"},
        {ColorInputStream, "i_color"},
        {AnchorIndexInputStream, "i_anchor_index"},
    };

    static const my::IndexName ubos[] = {
        {hrz::UboFrame, "Frame"},
        {TileParamsUbo, "Tile"},
        {AnchorParamsUbo, "AnchorPrototype"},
        {PlaceholderParamsUbo, "Placeholder"},
    };

    my::IndexName visual_samplers[] = {
        {AnchorDataTextureSamplerIndex, "u_anchors"},
        {hrz::SamplerCameraHeight, "u_camera_height"},
        {CullingVisibilitySamplerIndex, "u_visibility"},
    };

    static const char* outputs[] = {"o_color"};

    my::ShaderResource res{};
    res.name = hrz_shaders::Symbol_placeholder_name;
    res.vertex_source_len = hrz_shaders::Symbol_placeholder_vert_len;
    res.vertex_source = hrz_shaders::Symbol_placeholder_vert;
    res.fragment_source_len = hrz_shaders::Symbol_placeholder_frag_len;
    res.fragment_source = hrz_shaders::Symbol_placeholder_frag;
    res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
    res.uniform_blocks = ubos;
    res.output_count = HRZ_ARRAY_COUNT(outputs);
    res.outputs = outputs;
    res.attribs = attribs;
    res.attrib_count = HRZ_ARRAY_COUNT(attribs);
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

    res.name = hrz_shaders::Symbol_placeholder_picking_name;
    res.vertex_source_len = hrz_shaders::Symbol_placeholder_picking_vert_len;
    res.vertex_source = hrz_shaders::Symbol_placeholder_picking_vert;
    res.fragment_source_len = hrz_shaders::Symbol_placeholder_picking_frag_len;
    res.fragment_source = hrz_shaders::Symbol_placeholder_picking_frag;
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

    res.name = hrz_shaders::Symbol_placeholder_selection_name;
    res.vertex_source_len = hrz_shaders::Symbol_placeholder_selection_vert_len;
    res.vertex_source = hrz_shaders::Symbol_placeholder_selection_vert;
    res.fragment_source_len = hrz_shaders::Symbol_placeholder_selection_frag_len;
    res.fragment_source = hrz_shaders::Symbol_placeholder_selection_frag;
    res.output_count = HRZ_ARRAY_COUNT(selection_outputs);
    res.outputs = selection_outputs;
    res.sampler_count = HRZ_ARRAY_COUNT(selection_samplers);
    res.samplers = selection_samplers;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Symbols);
}

void PlaceholderElementSystem::init_render(Render* render)
{
    _visual_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_placeholder_name);
    _picking_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_placeholder_picking_name);
    _selection_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_placeholder_selection_name);

    {
        const lm::vec2 vertex_data[] = {{1, 0}, {1, 1}, {0, 0}, {0, 1}};

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = sizeof(vertex_data);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = (void*)&vertex_data[0].x;
        vb_res.allow_allocation_failure = false;

        _vertex_data_buffer = render->rc->alloc(
            &vb_res, hrz::monitoring::systems::Symbols,
            {{"contents"_ss, "placeholder vertex data"_ss}});
    }
}

void PlaceholderElementSystem::deinit_render(Render* render)
{
    for (auto resource : _unused_resources)
    {
        render->rc->dealloc(resource);
    }
    _unused_resources.clear();

    render->rc->dealloc(_vertex_data_buffer);
}

ElementSystem::PrototypeH PlaceholderElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
        register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>&)
{
    assert(element_descriptor.type() == ElementType);

    const auto& descriptor = element_descriptor.placeholder();

    Prototype prototype;
    prototype.layer_id = layer_id;
    prototype.z_index = z_index;

    prototype.baking_params.default_size = hrz::to_lm(descriptor.size().default_value());

    auto size_x_prp_name = fmt::format("{}_x", descriptor.size().name());
    auto size_y_prp_name = fmt::format("{}_y", descriptor.size().name());

    prototype.baking_params.size_prp.x =
        register_prp(size_x_prp_name, prototype.baking_params.default_size.x);
    prototype.baking_params.size_prp.y =
        register_prp(size_y_prp_name, prototype.baking_params.default_size.y);

    prototype.baking_params.default_color_srgb =
        hrz::convert_proto_color_to_bytes(descriptor.color().default_value());

    prototype.baking_params.color_prp = register_prp(
        descriptor.color().name(),
        vector_data::attr_from_color<vector_data::OwnedAttributeValue>(
            prototype.baking_params.default_color_srgb));

    prototype.status = Prototype::Status::Uploading;

    auto handle = _prototypes.alloc(std::move(prototype));

    _loading_prototypes.insert(handle);

    return {ElementType, handle};
}

void PlaceholderElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->baking_params.size_prp.x);
        unregister_property(prototype->baking_params.size_prp.y);
        unregister_property(prototype->baking_params.color_prp);
    }
}

void PlaceholderElementSystem::delete_prototype(PrototypeH prototype_handle)
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        _loading_prototypes.erase(prototype_handle.handle);
        _deleted_prototypes.insert(prototype_handle.handle);
    }
}

ElementSystem::PrototypeStatus PlaceholderElementSystem::get_prototype_status(
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

SymbolBakingData::ElementBakingParams PlaceholderElementSystem::get_prototype_baking_params(
    PrototypeH prototype_handle) const
{
    if (prototype_handle.type != ElementType)
    {
        assert(false);
        return {};
    }

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype == nullptr)
    {
        assert(false);
        return {};
    }

    return {prototype->baking_params};
}

std::optional<ElementSystem::RenderableH> PlaceholderElementSystem::make_renderable(
    PrototypeH prototype_handle,
    uint64_t layer_id,
    TileCoords tile_coords,
    BakedSymbols::ElementInstances&& baked_instances,
    double bsphere_radius,
    lm::dvec3 bsphere_center,
    my::ResourceHandle tile_ubo,
    my::ResourceHandle anchor_ubo,
    my::ResourceHandle anchor_data_texture,
    my::ResourceHandle selection_texture,
    const std::array<my::ResourceHandle, SCENE_VIEW_COUNT> culling_visibility_textures,
    my::ResourceHandle anchor_data_texture_sampler,
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
        std::get<hrz::BlobArray<BakedSymbols::PlaceholderInstance>>(baked_instances.data));
    auto instance_data = instance_blob.get_data();

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = instance_data.size_bytes();
    vb_res.usage = my::UsageHint::Static;
    vb_res.data = (void*)instance_data.data();
    vb_res.allow_allocation_failure = true;

    auto instance_data_buffer = render->rc->alloc(
        &vb_res, hrz::monitoring::systems::Symbols, layer_id,
        {{"contents"_ss, "placeholder instance data"_ss}, {"tile coords"_ss, tile_coords_str}});

    using PlaceholderInstance = BakedSymbols::PlaceholderInstance;

    my::VertexInputStream streams[] = {
        {InMeshPosInputStream, _vertex_data_buffer, my::VertexFormat::Float32_2, 0,
         sizeof(lm::vec2), my::VertexRate::PerVertex},
        {TransformInputStream + 0, instance_data_buffer, my::VertexFormat::Float32_4,
         offsetof(PlaceholderInstance, transform) + sizeof(lm::vec4) * 0,
         sizeof(PlaceholderInstance), my::VertexRate::PerInstance},
        {TransformInputStream + 1, instance_data_buffer, my::VertexFormat::Float32_4,
         offsetof(PlaceholderInstance, transform) + sizeof(lm::vec4) * 1,
         sizeof(PlaceholderInstance), my::VertexRate::PerInstance},
        {TransformInputStream + 2, instance_data_buffer, my::VertexFormat::Float32_4,
         offsetof(PlaceholderInstance, transform) + sizeof(lm::vec4) * 2,
         sizeof(PlaceholderInstance), my::VertexRate::PerInstance},
        {TransformInputStream + 3, instance_data_buffer, my::VertexFormat::Float32_4,
         offsetof(PlaceholderInstance, transform) + sizeof(lm::vec4) * 3,
         sizeof(PlaceholderInstance), my::VertexRate::PerInstance},
        {SizeInputStream, instance_data_buffer, my::VertexFormat::Float32_2,
         offsetof(PlaceholderInstance, size), sizeof(PlaceholderInstance),
         my::VertexRate::PerInstance},
        {ColorInputStream, instance_data_buffer, my::VertexFormat::UInt8Norm_4,
         offsetof(PlaceholderInstance, color), sizeof(PlaceholderInstance),
         my::VertexRate::PerInstance},
        {AnchorIndexInputStream, instance_data_buffer, my::VertexFormat::UInt32,
         offsetof(PlaceholderInstance, anchor_index), sizeof(PlaceholderInstance),
         my::VertexRate::PerInstance}};

    my::VertexInputResource vi_res;
    vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
    vi_res.attribs = streams;
    auto vertex_input = render->rc->alloc(
        &vi_res, hrz::monitoring::systems::Symbols, layer_id,
        {{"placeholder vertex input"_ss, tile_coords_str}});

    PlaceholderRenderable renderable;
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
    renderable.data.data_texture_sampler = anchor_data_texture_sampler;
    renderable.data.visual_shader = _visual_shader;
    renderable.data.picking_shader = _picking_shader;
    renderable.data.selection_shader = _selection_shader;
    renderable.data.tile_ubo = tile_ubo;
    renderable.data.placeholder_ubo = prototype->ubo;
    renderable.data.vertex_count = 4;
    renderable.data.instance_count = instance_data.size();

    return {{ElementType, _renderables.alloc(std::move(renderable))}};
}

void PlaceholderElementSystem::delete_renderable(RenderableH renderable_handle)
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

void PlaceholderElementSystem::draw_renderable(
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

void PlaceholderElementSystem::work(ReprSystem::WorkCtx& ctx)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol placeholder work");

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

void PlaceholderElementSystem::work_gpu(Render* render)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol placeholder work gpu");

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

        PlaceholderUniformData ubo;
        ubo.z_index = prototype->z_index;

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
