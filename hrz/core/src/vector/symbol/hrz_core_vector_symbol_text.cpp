#include "vector/symbol/hrz_core_vector_symbol_text.h"

#include "hrz_core_shaders.h"

#include <hrz_common_color.h>
#include <hrz_common_fmt.h>
#include <hrz_common_profiling.h>
#include <hrz_core_resources.h>

namespace hrz::vt::symbol
{
namespace
{
enum
{
    TextParamsUbo = ElementCustomUboStart,

    AnchorIndexTextureSamplerIndex = ElementCustomSamplerStart,
    TransformTextureSamplerIndex,
    OutlineWidthTextureSamplerIndex,
    FillColorTextureSamplerIndex,
    OutlineColorTextureSamplerIndex,
    FontTextureSamplerIndex,

    Uv0InputStream = 0,
    Uv1InputStream = 1,
    Uv2InputStream = 2,
    Uv3InputStream = 3,
    VertexIdInputStream = 4,
    TextIndexInputStream = 5,
};

struct TextUniformData
{
    uint32_t z_index;
    uint32_t _padding[3];
};

HRZ_CHECK_UBO_SIZE(TextUniformData);
} // namespace

void TextRenderable::render_callback(
    uint32_t render_type,
    my::RenderContext* r,
    my::ResourceBinder* rb,
    const void* user_data_raw,
    const void* raw_data)
{
    auto data = (const RenderData*)raw_data;
    const auto* user_data = (const hrz::SceneViewRenderGraphUserData*)user_data_raw;

    if (((1 << user_data->scene_view) & data->scene_views) == 0) return;

    my::ResourceHandle fill_shader;
    std::optional<my::ResourceHandle> outline_shader = std::nullopt;
    switch (render_type)
    {
        case hrz::RenderVisual:
            fill_shader = data->fill_visual_shader;
            outline_shader = data->outline_visual_shader;
            break;
        case hrz::RenderPicking: fill_shader = data->picking_shader; break;
        case hrz::RenderSelection:
            if (!data->has_any_selected) return;
            fill_shader = data->selection_shader;
            break;
        default: return;
    }

    auto batch =
        my::DrawBatchInfo(my::PrimitiveType::TriangleFan, 4).instanced(data->instance_count);

    rb->push_state();

    my::UboBinding ubo_bindings[] = {
        {TileParamsUbo, data->tile_ubo, 0, sizeof(TileUniformData)},
        {AnchorParamsUbo, data->anchor_ubo, 0, sizeof(AnchorUniformData)},
        {TextParamsUbo, data->text_ubo, 0, sizeof(TextUniformData)},
    };
    rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

    my::TextureBinding texture_bindings[] = {
        {AnchorDataTextureSamplerIndex, data->anchor_data_texture,
         data->anchor_data_texture_sampler},
        {SelectionSamplerIndex, data->selection_texture, data->anchor_data_texture_sampler},
        {CullingVisibilitySamplerIndex, data->culling_visibility_textures[user_data->scene_view],
         data->anchor_data_texture_sampler},
        {AnchorIndexTextureSamplerIndex, data->anchor_index_texture,
         data->text_data_texture_sampler},
        {OutlineWidthTextureSamplerIndex, data->outline_width_texture,
         data->text_data_texture_sampler},
        {TransformTextureSamplerIndex, data->transform_texture, data->text_data_texture_sampler},
        {FillColorTextureSamplerIndex, data->fill_color_texture, data->text_data_texture_sampler},
        {OutlineColorTextureSamplerIndex, data->outline_color_texture,
         data->text_data_texture_sampler},
        {FontTextureSamplerIndex, data->font_texture, data->font_texture_sampler},
    };
    rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

    auto state = rb->get_current_state();

    if (outline_shader.has_value() && data->has_non_zero_outline_width)
    {
        r->draw(
            batch, outline_shader.value(), data->vertex_input, state.ubo_count, state.ubos,
            state.texture_count, state.textures);
    }

    r->draw(
        batch, fill_shader, data->vertex_input, state.ubo_count, state.ubos, state.texture_count,
        state.textures);

    rb->pop_state();
}

TextElementSystem::FontH TextElementSystem::ref_font(
    const std::string& font_url,
    const hrz_proto::HttpHeaderList& headers)
{
    auto packed_headers = assets_loader::from_proto(headers);
    FontReference font_ref = {std::move(font_url), std::move(packed_headers)};

    auto it = _fonts_to_handles.find(font_ref);

    if (it != _fonts_to_handles.end())
    {
        auto font = _fonts.get_object(it->second);
        font->ref_count += 1;
        return it->second;
    }
    else
    {
        FontH handle = _fonts.alloc();
        auto font = _fonts.get_object(handle);
        font->status = Font::Status::New;
        font->ref_count = 1;
        font->load_ticket = 0;
        font->rasterizer_handle = 0;

        font->texture = my::ResourceHandle::null();

        _fonts_to_handles.insert({font_ref, handle});

        return handle;
    }
}

void TextElementSystem::unref_font(FontH handle)
{
    auto font = _fonts.get_object(handle);

    if (font != nullptr)
    {
        font->ref_count -= 1;
    }
    else
    {
        HRZ_LOG_WARNING("Cannot unuse unknown font with handle \"{}\"", handle);
    }
}

void TextElementSystem::collect_shaders(GpuResourceContext* rc)
{
    {
        my::IndexName attribs[] = {
            {Uv0InputStream, "i_in_text_position_uv_0"},
            {Uv1InputStream, "i_in_text_position_uv_1"},
            {Uv2InputStream, "i_in_text_position_uv_2"},
            {Uv3InputStream, "i_in_text_position_uv_3"},
            {VertexIdInputStream, "i_vertex_id"},
            {TextIndexInputStream, "i_text_index"},
        };

        my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {TileParamsUbo, "Tile"},
            {AnchorParamsUbo, "AnchorPrototype"},
            {TextParamsUbo, "Text"},
        };

        my::IndexName fill_samplers[] = {
            {AnchorDataTextureSamplerIndex, "u_anchors"},
            {CullingVisibilitySamplerIndex, "u_visibility"},
            {AnchorIndexTextureSamplerIndex, "u_anchor_indices"},
            {TransformTextureSamplerIndex, "u_transforms"},
            {FillColorTextureSamplerIndex, "u_fill_colors"},
            {FontTextureSamplerIndex, "u_font_texture"},
            {hrz::SamplerCameraHeight, "u_camera_height"},
        };

        static const char* outputs[] = {"o_color"};

        my::ShaderResource res{};
        res.name = hrz_shaders::Symbol_text_name;
        res.vertex_source_len = hrz_shaders::Symbol_text_vert_len;
        res.vertex_source = hrz_shaders::Symbol_text_vert;
        res.fragment_source_len = hrz_shaders::Symbol_text_frag_len;
        res.fragment_source = hrz_shaders::Symbol_text_frag;
        res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
        res.uniform_blocks = ubos;
        res.output_count = HRZ_ARRAY_COUNT(outputs);
        res.outputs = outputs;
        res.attribs = attribs;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.sampler_count = HRZ_ARRAY_COUNT(fill_samplers);
        res.samplers = fill_samplers;

        res.initial_state.depth.test = true;
        res.initial_state.depth.compare = my::DepthState::LessEqual;

        res.initial_state.color_blend.enable = true;
        res.initial_state.color_blend.color.src = my::ColorBlendState::SrcAlpha;
        res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;

        rc->alloc(&res, hrz::monitoring::systems::Symbols);

        my::IndexName outline_samplers[] = {
            {AnchorDataTextureSamplerIndex, "u_anchors"},
            {CullingVisibilitySamplerIndex, "u_visibility"},
            {AnchorIndexTextureSamplerIndex, "u_anchor_indices"},
            {TransformTextureSamplerIndex, "u_transforms"},
            {OutlineWidthTextureSamplerIndex, "u_outline_widths"},
            {OutlineColorTextureSamplerIndex, "u_outline_colors"},
            {FontTextureSamplerIndex, "u_font_texture"},
            {hrz::SamplerCameraHeight, "u_camera_height"},
        };

        res.name = hrz_shaders::Symbol_text_outline_name;
        res.vertex_source_len = hrz_shaders::Symbol_text_outline_vert_len;
        res.vertex_source = hrz_shaders::Symbol_text_outline_vert;
        res.fragment_source_len = hrz_shaders::Symbol_text_outline_frag_len;
        res.fragment_source = hrz_shaders::Symbol_text_outline_frag;
        res.sampler_count = HRZ_ARRAY_COUNT(outline_samplers);
        res.samplers = outline_samplers;

        rc->alloc(&res, hrz::monitoring::systems::Symbols);

        res.initial_state.depth.compare = my::DepthState::Equal;

        const char* picking_color_outputs[] = {"o_picking_id", "o_depth"};

        my::IndexName picking_samplers[] = {
            {CullingVisibilitySamplerIndex, "u_visibility"},
            {AnchorDataTextureSamplerIndex, "u_anchors"},
            {AnchorIndexTextureSamplerIndex, "u_anchor_indices"},
            {TransformTextureSamplerIndex, "u_transforms"},
            {OutlineWidthTextureSamplerIndex, "u_outline_widths"},
            {FillColorTextureSamplerIndex, "u_fill_colors"},
            {OutlineColorTextureSamplerIndex, "u_outline_colors"},
            {FontTextureSamplerIndex, "u_font_texture"},
            {hrz::SamplerCameraHeight, "u_camera_height"},
        };

        res.name = hrz_shaders::Symbol_text_picking_name;
        res.vertex_source_len = hrz_shaders::Symbol_text_picking_vert_len;
        res.vertex_source = hrz_shaders::Symbol_text_picking_vert;
        res.fragment_source_len = hrz_shaders::Symbol_text_picking_frag_len;
        res.fragment_source = hrz_shaders::Symbol_text_picking_frag;
        res.output_count = HRZ_ARRAY_COUNT(picking_color_outputs);
        res.outputs = picking_color_outputs;
        res.sampler_count = HRZ_ARRAY_COUNT(picking_samplers);
        res.samplers = picking_samplers;
        res.initial_state.color_blend.enable = false;

        res.initial_state.depth.compare = my::DepthState::LessEqual;

        rc->alloc(&res, hrz::monitoring::systems::Symbols);

        const char* selection_color_outputs[] = {"o_highlight"};

        my::IndexName selection_samplers[] = {
            {CullingVisibilitySamplerIndex, "u_visibility"},
            {AnchorDataTextureSamplerIndex, "u_anchors"},
            {SelectionSamplerIndex, "u_selection"},
            {AnchorIndexTextureSamplerIndex, "u_anchor_indices"},
            {TransformTextureSamplerIndex, "u_transforms"},
            {OutlineWidthTextureSamplerIndex, "u_outline_widths"},
            {FillColorTextureSamplerIndex, "u_fill_colors"},
            {OutlineColorTextureSamplerIndex, "u_outline_colors"},
            {FontTextureSamplerIndex, "u_font_texture"},
            {hrz::SamplerCameraHeight, "u_camera_height"},
        };

        res.name = hrz_shaders::Symbol_text_selection_name;
        res.vertex_source_len = hrz_shaders::Symbol_text_selection_vert_len;
        res.vertex_source = hrz_shaders::Symbol_text_selection_vert;
        res.fragment_source_len = hrz_shaders::Symbol_text_selection_frag_len;
        res.fragment_source = hrz_shaders::Symbol_text_selection_frag;
        res.output_count = HRZ_ARRAY_COUNT(selection_color_outputs);
        res.outputs = selection_color_outputs;
        res.sampler_count = HRZ_ARRAY_COUNT(selection_samplers);
        res.samplers = selection_samplers;
        res.initial_state.color_blend.enable = false;

        rc->alloc(&res, hrz::monitoring::systems::Symbols);
    }
}

void TextElementSystem::deinit(ReprSystem::WorkCtx& ctx, Render* render)
{
    for (auto& it : _fonts_to_handles)
    {
        auto* font = _fonts.get_object(it.second);

        if (font->status == Font::Status::Loading)
        {
            assets_loader::end(ctx.al, font->load_ticket);
        }
        else if (font->status == Font::Status::Loaded)
        {
            font_rasterizer::remove_font(ctx.fr, font->rasterizer_handle);
        }

        _fonts.release(it.second);
    }
}

void TextElementSystem::init_render(Render* render)
{
    _fill_visual_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_text_name);
    _outline_visual_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_text_outline_name);
    _picking_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_text_picking_name);
    _selection_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_text_selection_name);

    {
        // This vertex attribute should be useless, as it replicates
        // the gl_VertexID built-in variable.
        // However some platforms require that at least one vertex
        // attribute with a per-vertex advance rate be present.

        uint8_t data[4] = {0, 1, 2, 3};

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = sizeof(uint8_t) * HRZ_ARRAY_COUNT(data);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = data;

        _vertex_id_buffer = render->rc->alloc(&vb_res, hrz::monitoring::systems::Symbols);
    }

    {
        my::SamplerResource res;
        res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
        res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
        res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
        res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
        res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
        res.use_mipmaps = false;

        _data_texture_sampler = render->rc->alloc(&res, hrz::monitoring::systems::Symbols);
    }

    {
        my::SamplerResource res;
        res.sampler.min_filter = my::SamplerParams::Filter::Linear;
        res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
        res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
        res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
        res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
        res.use_mipmaps = false;

        _font_texture_sampler = render->rc->alloc(&res, hrz::monitoring::systems::Symbols);
    }
}

void TextElementSystem::deinit_render(Render* render)
{
    for (auto resource : _unused_resources)
    {
        render->rc->dealloc(resource);
    }
    _unused_resources.clear();

    render->rc->dealloc(_vertex_id_buffer);
    render->rc->dealloc(_data_texture_sampler);
    render->rc->dealloc(_font_texture_sampler);
}

ElementSystem::PrototypeH TextElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
        register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>&)
{
    assert(element_descriptor.type() == ElementType);

    const auto& descriptor = element_descriptor.text();

    Prototype prototype;
    prototype.layer_id = layer_id;
    prototype.z_index = z_index;
    prototype.font = ref_font(descriptor.font_url(), descriptor.font_http_headers());

    prototype.baking_params.default_text = descriptor.text().default_value();
    prototype.baking_params.text_prp =
        register_prp(descriptor.text().name(), prototype.baking_params.default_text);
    prototype.baking_params.default_font_size = descriptor.font_size().default_value();
    prototype.baking_params.font_size_prp =
        register_prp(descriptor.font_size().name(), prototype.baking_params.default_font_size);
    prototype.baking_params.default_fill_color =
        hrz::convert_proto_color_to_bytes(descriptor.text_color().default_value());
    prototype.baking_params.fill_color_prp = register_prp(
        descriptor.text_color().name(),
        hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
            prototype.baking_params.default_fill_color));
    prototype.baking_params.default_outline_size =
        std::max(descriptor.outline_width().default_value(), 0.0f);
    prototype.baking_params.outline_size_prp = register_prp(
        descriptor.outline_width().name(), prototype.baking_params.default_outline_size);
    prototype.baking_params.outline_size_unit = descriptor.outline_width_unit();
    prototype.baking_params.default_outline_color =
        hrz::convert_proto_color_to_bytes(descriptor.outline_color().default_value());
    prototype.baking_params.outline_color_prp = register_prp(
        descriptor.outline_color().name(),
        hrz::vector_data::attr_from_color<hrz::vector_data::OwnedAttributeValue>(
            prototype.baking_params.default_outline_color));
    prototype.baking_params.default_alignment = descriptor.alignment().default_value();
    prototype.baking_params.alignment_prp = register_prp(
        descriptor.alignment().name(), (int64_t)prototype.baking_params.default_alignment);
    prototype.baking_params.default_line_spacing =
        std::max(descriptor.line_spacing().default_value(), 0.0f);
    prototype.baking_params.line_spacing_prp = register_prp(
        descriptor.line_spacing().name(), prototype.baking_params.default_line_spacing);

    prototype.status = Prototype::Status::WaitingForFont;

    auto handle = _prototypes.alloc(std::move(prototype));

    _loading_prototypes.insert(handle);

    return {ElementType, handle};
}

void TextElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype != nullptr)
    {
        unregister_property(prototype->baking_params.text_prp);
        unregister_property(prototype->baking_params.font_size_prp);
        unregister_property(prototype->baking_params.fill_color_prp);
        unregister_property(prototype->baking_params.outline_size_prp);
        unregister_property(prototype->baking_params.outline_color_prp);
        unregister_property(prototype->baking_params.alignment_prp);
        unregister_property(prototype->baking_params.line_spacing_prp);
    }
}

void TextElementSystem::delete_prototype(PrototypeH prototype_handle)
{
    if (prototype_handle.type != ElementType) return;

    if (_prototypes.is_valid(prototype_handle.handle))
    {
        _loading_prototypes.erase(prototype_handle.handle);
        _deleted_prototypes.insert(prototype_handle.handle);
    }
}

ElementSystem::PrototypeStatus TextElementSystem::get_prototype_status(
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

    auto font = _fonts.get_object(prototype->font);
    if (font == nullptr)
    {
        return PrototypeStatus::Error;
    }

    switch (font->status)
    {
        case Font::Status::New:
        case Font::Status::Loading:
        case Font::Status::Uploading:
        case Font::Status::Allocating: return PrototypeStatus::Loading;
        case Font::Status::Loaded: return PrototypeStatus::Ready;
        default: return PrototypeStatus::Error;
    }
}

SymbolBakingData::ElementBakingParams TextElementSystem::get_prototype_baking_params(
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

std::optional<ElementSystem::RenderableH> TextElementSystem::make_renderable(
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

    auto font = _fonts.get_object(prototype->font);
    if (font == nullptr)
    {
        HRZ_LOG_ERROR("Could not find font");
        return std::nullopt;
    }

#define CHECK_RESOURCE_UPLOAD(resource, resource_type)                                \
    if (resource.is_null())                                                           \
    {                                                                                 \
        HRZ_LOG_ERROR(                                                                \
            "Could not upload texture {} of tile {}-{}-{} to the GPU", resource_type, \
            tile_coords.lod, tile_coords.x, tile_coords.y);                           \
        return std::nullopt;                                                          \
    }

    auto tile_coords_str = fmt::to_string(tile_coords);

    auto instance_data = std::get<BakedSymbols::TextInstances>(std::move(baked_instances.data));

    TextRenderable renderable;
    renderable.center = bsphere_center;
    renderable.radius = bsphere_radius;
    renderable.z_index = z_index & ~0xff;
    renderable.data.has_non_zero_outline_width = instance_data.has_non_zero_outline_width;
    renderable.data.instance_count = instance_data.positions_uvs.size();

    auto positions_uvs = instance_data.positions_uvs.get_data();
    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = positions_uvs.size_bytes();
    vb_res.usage = my::UsageHint::Static;
    vb_res.data = (const void*)positions_uvs.data();
    vb_res.allow_allocation_failure = true;

    renderable.pos_uv_buffer = render->rc->alloc(
        &vb_res, hrz::monitoring::systems::Symbols, layer_id,
        {{"tile coords"_ss, tile_coords_str}, {"contents"_ss, "text glyph positions & uv"_ss}});
    CHECK_RESOURCE_UPLOAD(renderable.pos_uv_buffer, "position & uv buffer");

    auto make_pos_uv_stream = [&](unsigned int index)
    {
        my::VertexInputStream pos_uv_stream;
        pos_uv_stream.index = index;
        pos_uv_stream.buffer = renderable.pos_uv_buffer;
        pos_uv_stream.format = my::VertexFormat::Float32_4;
        pos_uv_stream.offset = my::vertex_size(my::VertexFormat::Float32_4) * index;
        pos_uv_stream.stride = my::vertex_size(my::VertexFormat::Float32_4) * 4;
        pos_uv_stream.rate = my::VertexRate::PerInstance;
        return pos_uv_stream;
    };

    my::VertexInputStream vertex_id_stream;
    vertex_id_stream.index = VertexIdInputStream;
    vertex_id_stream.buffer = _vertex_id_buffer;
    vertex_id_stream.format = my::VertexFormat::UInt8;
    vertex_id_stream.offset = 0;
    vertex_id_stream.stride = my::vertex_size(my::VertexFormat::UInt8);
    vertex_id_stream.rate = my::VertexRate::PerVertex;

    auto text_indices = instance_data.text_indices.get_data();
    my::BufferResource text_index_res(my::BufferResource::BufferType::Vertex);
    text_index_res.size = text_indices.size_bytes();
    text_index_res.usage = my::UsageHint::Static;
    text_index_res.data = (const void*)text_indices.data();
    text_index_res.allow_allocation_failure = true;

    renderable.text_index_buffer = render->rc->alloc(
        &text_index_res, hrz::monitoring::systems::Symbols, layer_id,
        {{"tile coords"_ss, tile_coords_str}, {"contents"_ss, "text glyph text indices"_ss}});
    CHECK_RESOURCE_UPLOAD(renderable.text_index_buffer, "index buffer");

    my::VertexInputStream text_index_stream;
    text_index_stream.index = TextIndexInputStream;
    text_index_stream.buffer = renderable.text_index_buffer;
    text_index_stream.format = my::VertexFormat::UInt16;
    text_index_stream.offset = 0;
    text_index_stream.stride = my::vertex_size(my::VertexFormat::UInt16);
    text_index_stream.rate = my::VertexRate::PerInstance;

    {
        my::VertexInputStream streams[] = {
            make_pos_uv_stream(Uv0InputStream),
            make_pos_uv_stream(Uv1InputStream),
            make_pos_uv_stream(Uv2InputStream),
            make_pos_uv_stream(Uv3InputStream),
            vertex_id_stream,
            text_index_stream};

        my::VertexInputResource vi_res;
        vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
        vi_res.attribs = streams;
        renderable.data.vertex_input = render->rc->alloc(
            &vi_res, hrz::monitoring::systems::Symbols, layer_id,
            {{"tile coords"_ss, tile_coords_str}, {"contents"_ss, "text vertex input"_ss}});
    }

    auto alloc_data_texture = [&](gsl::span<const std::byte> data_buffer, my::TextureFormat format,
                                  unsigned int pixels_per_feature,
                                  hrz::MetadataString contents_metadata)
    {
        assert(!my::is_format_compressed(format));
        size_t bytes_per_feature = my::format_external_pixel_byte_size(format) * pixels_per_feature;
        size_t feature_count = data_buffer.size() / bytes_per_feature;
        lm::uvec2 texture_size = hrz::vt::compute_data_texture_size(feature_count);

        my::TextureResource tex_res;
        tex_res.layout.type = my::TextureLayout::Type2D;
        tex_res.layout.format = format;
        tex_res.layout.width = texture_size.x * pixels_per_feature;
        tex_res.layout.height = texture_size.y;
        tex_res.layout.depth = 1;
        tex_res.layout.levels = 1;
        tex_res.data = {&data_buffer, 1};
        tex_res.generate_mipmaps = false;
        tex_res.allow_allocation_failure = true;

        auto texture = render->rc->alloc(
            &tex_res, hrz::monitoring::systems::Symbols, layer_id,
            {{"contents"_ss, contents_metadata}, {"tile coords"_ss, tile_coords_str}});

        return texture;
    };

    {
        auto data = instance_data.anchor_indices.get_data();
        renderable.data.anchor_index_texture = alloc_data_texture(
            hrz::as_bytes(data.as_span()), my::TextureFormat::R32UI, 1,
            "text anchor index texture"_ss);
        CHECK_RESOURCE_UPLOAD(renderable.data.anchor_index_texture, "text anchor index texture");
    }
    {
        auto data = instance_data.transforms.get_data();
        renderable.data.transform_texture = alloc_data_texture(
            hrz::as_bytes(data.as_span()), my::TextureFormat::RGBA32F, 4,
            "text transform texture"_ss);
        CHECK_RESOURCE_UPLOAD(renderable.data.transform_texture, "text transform texture");
    }
    {
        auto data = instance_data.outline_widths.get_data();
        renderable.data.outline_width_texture = alloc_data_texture(
            hrz::as_bytes(data.as_span()), my::TextureFormat::R32F, 1,
            "text outline width texture"_ss);
        CHECK_RESOURCE_UPLOAD(renderable.data.outline_width_texture, "text outline width texture");
    }
    {
        auto data = instance_data.fill_colors.get_data();
        renderable.data.fill_color_texture = alloc_data_texture(
            hrz::as_bytes(data.as_span()), my::TextureFormat::RGBA8, 1,
            "text fill color texture"_ss);
        CHECK_RESOURCE_UPLOAD(renderable.data.fill_color_texture, "text fill color texture");
    }
    {
        auto data = instance_data.outline_colors.get_data();
        renderable.data.outline_color_texture = alloc_data_texture(
            hrz::as_bytes(data.as_span()), my::TextureFormat::RGBA8, 1,
            "text outline color texture"_ss);
        CHECK_RESOURCE_UPLOAD(renderable.data.outline_color_texture, "text outline color texture");
    }

    renderable.data.text_data_texture_sampler = _data_texture_sampler;

    renderable.data.font_texture = font->texture;
    renderable.data.font_texture_sampler = _font_texture_sampler;

    renderable.data.anchor_ubo = anchor_ubo;
    renderable.data.anchor_data_texture = anchor_data_texture;
    renderable.data.anchor_data_texture_sampler = anchor_data_texture_sampler;
    renderable.data.selection_texture = selection_texture;
    renderable.data.culling_visibility_textures = culling_visibility_textures;

    renderable.data.fill_visual_shader = _fill_visual_shader;
    renderable.data.outline_visual_shader = _outline_visual_shader;
    renderable.data.picking_shader = _picking_shader;
    renderable.data.selection_shader = _selection_shader;

    renderable.data.tile_ubo = tile_ubo;
    renderable.data.text_ubo = prototype->ubo;

#undef CHECK_RESOURCE_UPLOAD

    return {{ElementType, _renderables.alloc(std::move(renderable))}};
}

void TextElementSystem::delete_renderable(RenderableH renderable_handle)
{
    if (renderable_handle.type != ElementType) return;

    auto renderable = _renderables.get_object(renderable_handle.handle);
    if (renderable)
    {
        _unused_resources.push_back(renderable->pos_uv_buffer);
        _unused_resources.push_back(renderable->text_index_buffer);
        _unused_resources.push_back(renderable->data.vertex_input);
        _unused_resources.push_back(renderable->data.anchor_index_texture);
        _unused_resources.push_back(renderable->data.transform_texture);
        _unused_resources.push_back(renderable->data.outline_width_texture);
        _unused_resources.push_back(renderable->data.fill_color_texture);
        _unused_resources.push_back(renderable->data.outline_color_texture);

        _renderables.release(renderable_handle.handle);
    }
}

void TextElementSystem::draw_renderable(
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

void TextElementSystem::work(ReprSystem::WorkCtx& ctx)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol text work");

    for (auto handle : _deleted_prototypes)
    {
        auto prototype = _prototypes.get_object(handle);
        if (!prototype) continue;

        unref_font(prototype->font);

        _unused_resources.push_back(prototype->ubo);

        _prototypes.release(handle);
    }
    _deleted_prototypes.clear();

    for (auto it = _fonts_to_handles.begin(); it != _fonts_to_handles.end();)
    {
        auto handle = it->second;
        auto font = _fonts.get_object(handle);

        if (font->ref_count == 0)
        {
            if (font->status == Font::Status::Loading)
            {
                assets_loader::end(ctx.al, font->load_ticket);
            }
            else if (font->status == Font::Status::Allocating)
            {
                blobs::cancel(ctx.ba, font->blob_ticket);
            }
            else if (font->status == Font::Status::Uploading)
            {
                font->blob.release();
            }
            else if (font->status == Font::Status::Loaded)
            {
                font_rasterizer::remove_font(ctx.fr, font->rasterizer_handle);
            }

            _unused_resources.push_back(font->texture);

            _fonts.release(handle);
            _fonts_to_handles.erase(it++);
            continue;
        }

        if (font->status == Font::Status::New)
        {
            if (!it->first.url.empty())
            {
                font->url = it->first.url;
                font->load_ticket = assets_loader::begin(
                    ctx.al, it->first.url, it->first.headers, assets_loader::Queue::VectorData,
                    assets_loader::MAX_PRIORITY, {monitoring::systems::Symbols});
                font->status = Font::Status::Loading;
            }
            else
            {
                // No URL has been provided, using the default font.
                auto rasterizer_handle_opt = font_rasterizer::add_font(
                    ctx.fr, hrz_res::get_data(hrz_res::Resources::DefaultFont));
                if (rasterizer_handle_opt.has_value())
                {
                    font->rasterizer_handle = rasterizer_handle_opt.value();
                    font->status = Font::Status::Allocating;
                }
                else
                {
                    HRZ_LOG_ERROR("Could not parse default font");
                    font->status = Font::Status::Error;
                }
            }
        }
        else if (font->status == Font::Status::Loading)
        {
            if (assets_loader::is_finished(ctx.al, font->load_ticket))
            {
                if (assets_loader::get_status(ctx.al, font->load_ticket)
                    == assets_loader::RequestStatus::Loaded)
                {
                    auto rasterizer_handle_opt = font_rasterizer::add_font(
                        ctx.fr, assets_loader::get_blob(ctx.al, ctx.ba, font->load_ticket));
                    if (rasterizer_handle_opt.has_value())
                    {
                        font->rasterizer_handle = rasterizer_handle_opt.value();
                        font->status = Font::Status::Allocating;
                    }
                    else
                    {
                        HRZ_LOG_ERROR("Could not parse font at \"{}\"", font->url);
                        font->status = Font::Status::Error;
                    }
                }
                else
                {
                    font->status = Font::Status::Error;
                }

                assets_loader::end(ctx.al, font->load_ticket);
            }
        }
        else if (font->status == Font::Status::Allocating)
        {
            if (!font->blob_ticket.is_valid())
            {
                font->blob_ticket = blobs::allocate_blob(
                    ctx.ba, font_rasterizer::TEXTURE_SIZE * font_rasterizer::TEXTURE_SIZE * 3,
                    false);
                blobs::register_owner(ctx.ba, font->blob_ticket, {monitoring::systems::Symbols});
                blobs::register_metadata(
                    ctx.ba, font->blob_ticket, "type"_ss, "font texture initial data"_ss);
                blobs::register_metadata(ctx.ba, font->blob_ticket, "URL"_ss, font->url);
            }
            else
            {
                auto state = blobs::get_state(ctx.ba, font->blob_ticket);
                if (state == blobs::BlobState::Allocated)
                {
                    font->blob = blobs::to_blob(ctx.ba, font->blob_ticket);
                    font->status = Font::Status::Uploading;
                }
                else if (state == blobs::BlobState::Error)
                {
                    HRZ_LOG_WARNING(
                        "Could not initialize blob for texture. "
                        "Some graphical artifacts may be present.");
                    font->status = Font::Status::Uploading;
                }
            }
        }
        else if (font->status == Font::Status::Loaded)
        {
            auto new_glyphs_opt = font_rasterizer::get_new_glyphs(ctx.fr, font->rasterizer_handle);

            if (new_glyphs_opt.has_value())
            {
                if (font->new_glyphs.empty())
                {
                    font->new_glyphs = std::move(new_glyphs_opt.value());
                }
                else
                {
                    font->new_glyphs.insert(
                        font->new_glyphs.end(), new_glyphs_opt.value().begin(),
                        new_glyphs_opt.value().end());
                }
            }
        }

        ++it;
    }

    for (auto it = _loading_prototypes.begin(); it != _loading_prototypes.end();)
    {
        auto prototype = _prototypes.get_object(*it);
        bool erase = false;

        if (!prototype)
        {
            _loading_prototypes.erase(it++);
            continue;
        }

        if (prototype->status == Prototype::Status::WaitingForFont)
        {
            auto font = _fonts.get_object(prototype->font);

            if (font->status == Font::Status::Error)
            {
                HRZ_LOG_ERROR("Could not load font");
                prototype->status = Prototype::Status::Error;
                erase = true;
            }
            else if (font->status == Font::Status::Loaded)
            {
                prototype->baking_params.font = font->rasterizer_handle;
                prototype->status = Prototype::Status::Uploading;
            }
        }

        if (erase)
        {
            _loading_prototypes.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

void TextElementSystem::work_gpu(Render* render)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol text work gpu");

    for (auto resource : _unused_resources)
    {
        render->rc->dealloc(resource);
    }
    _unused_resources.clear();

    for (auto& it : _fonts_to_handles)
    {
        auto font = _fonts.get_object(it.second);

        if (font->status == Font::Status::Uploading)
        {
            assert(font->texture.is_null());

            // Initialise the texture with middle grey, in order to minimise
            // artefacts when drawing glyphs at small scales.
            // This is because discontinuities in the SDF when crossing from
            // one glyph slot to its neighbour make the aastep() function in
            // the fragment shader wrongly estimate the fragment size.
            // Between two filled slots the magnitude of the discontinuity is
            // usually small, but between a filled slot and an empty one it
            // can be large if the empty pixels have extreme values.
            // The best way to decrease the magnitude of the discontinuity is
            // to set the value of empty pixels to the medium value.

            auto alloc_texture = [&](gsl::span<const std::byte> data_span)
            {
                my::TextureResource res;
                res.layout.type = my::TextureLayout::Type2D;
                res.layout.format = my::TextureFormat::RGB8;
                res.layout.width = font_rasterizer::TEXTURE_SIZE;
                res.layout.height = font_rasterizer::TEXTURE_SIZE;
                res.layout.depth = 1;
                res.layout.levels = 1;
                res.data = {&data_span, 1};
                res.generate_mipmaps = false;
                res.allow_allocation_failure = true;

                // A font can be used by multiple layers, so the resource cannot
                // be associated to one layer in particular.
                font->texture = render->rc->alloc(
                    &res, monitoring::systems::Symbols, monitoring::NoLayer,
                    {{"contents"_ss, "font SDF"_ss}, {"source URL"_ss, font->url}});

                if (!font->texture.is_null())
                {
                    font->status = Font::Status::Loaded;
                }
                else
                {
                    HRZ_LOG_ERROR(
                        "Could not upload text font texture \"{}\" to the GPU", font->url);
                    font->status = Font::Status::Error;
                }
            };

            if (font->blob.is_valid())
            {
                auto blob_data = font->blob.get_mutable_data();
                std::memset(blob_data.data(), 0x7f, blob_data.size());
                alloc_texture(blob_data);
                font->blob.release();
            }
            else
            {
                alloc_texture({nullptr, 0});
            }

            font->new_glyphs.clear();
        }
        else if (font->status == Font::Status::Loaded && !font->new_glyphs.empty())
        {
            // Upload newly rasterised glyphs to the font texture.

            static constexpr unsigned int slot_size = font_rasterizer::GLYPH_SLOT_SIZE;

            for (const auto& glyph : font->new_glyphs)
            {
                if (glyph.info.is_blank) continue;

                auto index = glyph.info.in_texture_index;
                unsigned int glyph_x = index % (font_rasterizer::TEXTURE_SIZE / slot_size);
                unsigned int glyph_y = index / (font_rasterizer::TEXTURE_SIZE / slot_size);

                render->my->update_texture(
                    font->texture, my::TextureFormat::RGB8, 0, glyph_x * slot_size,
                    glyph_y * slot_size, 0, slot_size, slot_size, 1,
                    hrz::as_bytes(glyph.raster_span()));
            }

            font->new_glyphs.clear();
        }
    }

    for (auto it = _loading_prototypes.begin(); it != _loading_prototypes.end();)
    {
        auto prototype = _prototypes.get_object(*it);

        if (!prototype)
        {
            _loading_prototypes.erase(it++);
            continue;
        }

        if (prototype->status == Prototype::Status::Uploading)
        {
            TextUniformData ubo;
            ubo.z_index = prototype->z_index;

            my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
            ub_res.size = sizeof(ubo);
            ub_res.usage = my::UsageHint::Static;
            ub_res.data = &ubo;
            prototype->ubo =
                render->rc->alloc(&ub_res, hrz::monitoring::systems::Symbols, prototype->layer_id);

            prototype->status = Prototype::Status::Ready;

            _loading_prototypes.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}
} // namespace hrz::vt::symbol
