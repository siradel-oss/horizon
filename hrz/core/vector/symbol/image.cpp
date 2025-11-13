#include "hrz/core/vector/symbol/image.h"

#include "hrz/common/color.h"
#include "hrz/common/fmt.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proto_maths.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/mem.h"

#include <numeric>

namespace hrz::vt::symbol
{
namespace
{
enum
{
    ImageParamsUbo = ElementCustomUboStart,

    ImageSamplerIndex = ElementCustomSamplerStart,

    InPosFixedStretchyInputStream = 0,
    UvInputStream = 1,
    TransformInputStream = 2,
    StretchSizeInputStream = 6,
    ColorInputStream = 7,
    AnchorIndexInputStream = 8,
    UvOffsetInputStream = 9,
    UvSizeInputStream = 10,
};

struct ImageUniformData
{
    uint32_t z_index;
    uint32_t blend_mode;
    float blend_strength;
    uint32_t _padding[1];
};

HRZ_CHECK_UBO_SIZE(ImageUniformData);
} // namespace

void ImageRenderable::render_callback(
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

    rb->push_state();

    my::UboBinding ubo_bindings[] = {
        {TileParamsUbo, data->tile_ubo, 0, sizeof(TileUniformData)},
        {AnchorParamsUbo, data->anchor_ubo, 0, sizeof(AnchorUniformData)},
        {ImageParamsUbo, data->image_ubo, 0, sizeof(ImageUniformData)},
    };
    rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

    my::TextureBinding texture_bindings[] = {
        {AnchorDataTextureSamplerIndex, data->anchor_data_texture, data->data_texture_sampler},
        {SelectionSamplerIndex, data->selection_texture, data->data_texture_sampler},
        {CullingVisibilitySamplerIndex, data->culling_visibility_textures[user_data->scene_view],
         data->data_texture_sampler},
        {ImageSamplerIndex, data->image_texture, data->image_texture_sampler},
    };
    rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

    auto state = rb->get_current_state();

    for (const auto& batch_info : data->batches)
    {
        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, batch_info.index_count)
                         .indexed(my::IndexType::UShort, batch_info.first_index)
                         .instanced(batch_info.instance_count);

        r->draw(
            batch, shader, batch_info.vertex_input, state.ubo_count, state.ubos,
            state.texture_count, state.textures);
    }

    rb->pop_state();
}

void ImageElementSystem::collect_shaders(hrz::GpuResourceContext* rc)
{
    my::IndexName attribs[] = {
        {InPosFixedStretchyInputStream, "i_pos_fixed_stretchy"},
        {UvInputStream, "i_uv"},
        {TransformInputStream, "i_transform"},
        {StretchSizeInputStream, "i_stretch_size"},
        {ColorInputStream, "i_color"},
        {AnchorIndexInputStream, "i_anchor_index"},
        {UvOffsetInputStream, "i_uv_offset"},
        {UvSizeInputStream, "i_uv_size"},
    };

    static const my::IndexName ubos[] = {
        {hrz::UboFrame, "Frame"},
        {TileParamsUbo, "Tile"},
        {AnchorParamsUbo, "AnchorPrototype"},
        {ImageParamsUbo, "Image"},
    };

    my::IndexName visual_samplers[] = {
        {AnchorDataTextureSamplerIndex, "u_anchors"},
        {CullingVisibilitySamplerIndex, "u_visibility"},
        {ImageSamplerIndex, "u_image"},
        {hrz::SamplerCameraHeight, "u_camera_height"},
    };

    static const char* outputs[] = {"o_color"};

    my::ShaderResource res{};
    res.name = hrz_shaders::Symbol_image_name;
    res.vertex_source_len = hrz_shaders::Symbol_image_vert_len;
    res.vertex_source = hrz_shaders::Symbol_image_vert;
    res.fragment_source_len = hrz_shaders::Symbol_image_frag_len;
    res.fragment_source = hrz_shaders::Symbol_image_frag;
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

    res.name = hrz_shaders::Symbol_image_picking_name;
    res.vertex_source_len = hrz_shaders::Symbol_image_picking_vert_len;
    res.vertex_source = hrz_shaders::Symbol_image_picking_vert;
    res.fragment_source_len = hrz_shaders::Symbol_image_picking_frag_len;
    res.fragment_source = hrz_shaders::Symbol_image_picking_frag;
    res.output_count = HRZ_ARRAY_COUNT(picking_color_outputs);
    res.outputs = picking_color_outputs;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Symbols);

    static const char* selection_outputs[] = {"o_highlight"};

    my::IndexName selection_samplers[] = {
        {AnchorDataTextureSamplerIndex, "u_anchors"},    {SelectionSamplerIndex, "u_selection"},
        {CullingVisibilitySamplerIndex, "u_visibility"}, {ImageSamplerIndex, "u_image"},
        {hrz::SamplerCameraHeight, "u_camera_height"},
    };

    res.name = hrz_shaders::Symbol_image_selection_name;
    res.vertex_source_len = hrz_shaders::Symbol_image_selection_vert_len;
    res.vertex_source = hrz_shaders::Symbol_image_selection_vert;
    res.fragment_source_len = hrz_shaders::Symbol_image_selection_frag_len;
    res.fragment_source = hrz_shaders::Symbol_image_selection_frag;
    res.output_count = HRZ_ARRAY_COUNT(selection_outputs);
    res.outputs = selection_outputs;
    res.sampler_count = HRZ_ARRAY_COUNT(selection_samplers);
    res.samplers = selection_samplers;
    res.initial_state.color_blend.enable = false;
    rc->alloc(&res, hrz::monitoring::systems::Symbols);
}

void ImageElementSystem::init_render(Render* render)
{
    _visual_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_image_name);
    _picking_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_image_picking_name);
    _selection_shader = render->rc->retrieve_shader(hrz_shaders::Symbol_image_selection_name);

    {
        my::SamplerResource res;
        res.sampler.min_filter = my::SamplerParams::Filter::Linear;
        res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
        res.sampler.mipmap_filter = my::SamplerParams::Filter::Linear;
        res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
        res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
        res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
        res.use_mipmaps = true;

        _simple_image_sampler = render->rc->alloc(&res, hrz::monitoring::systems::Symbols);
    }

    {
        my::SamplerResource res;
        res.sampler.min_filter = my::SamplerParams::Filter::Linear;
        res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
        res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
        res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
        res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
        // If we enable mipmaps, we can get awful looking lines in the sprites when
        // regions are reduced because it fetches a lower mip level, or when
        // multiple sprites bleed into one another.
        res.use_mipmaps = false;

        _sprite_image_sampler = render->rc->alloc(&res, hrz::monitoring::systems::Symbols);
    }
}

void ImageElementSystem::deinit_render(Render* render)
{
    for (auto resource : _unused_resources)
    {
        render->rc->dealloc(resource);
    }
    _unused_resources.clear();

    render->rc->dealloc(_simple_image_sampler);
    render->rc->dealloc(_sprite_image_sampler);
}

// Structure used to build the cuts in a sprite that define stretchable and non-stretchable regions.
struct SpriteCuts
{
    struct Cut
    {
        int pos;
        bool stretch;
    };

    hrz::InlinedVector<Cut, 16> cuts;

    // Inserts a new cut while trying to merge neighboring stuff. Returns whether successful or
    // not. It can be unsuccessful if cuts overlap, in which case the second part of a stretch
    // region shouldn't be inserted.
    bool insert_cut(int pos, bool stretch, bool should_merge = true)
    {
        if (!cuts.empty())
        {
            if (cuts.back().pos > pos)
            {
                // Overlap!
                return false;
            }
            else if (cuts.back().pos == pos)
            {
                // Same position -> remove the last cut and retry.
                // This can't recurse infinitely because eventually we have no more cuts.
                cuts.resize(cuts.size() - 1);
                return insert_cut(pos, stretch);
                return true;
            }
            else if (should_merge && cuts.back().stretch == stretch)
            {
                // Two neighboring stretchy or non-stretchy, we don't need to insert, this merges
                // them.
                return true;
            }
        }

        cuts.push_back({pos, stretch});
        return true;
    }

    void make_cuts_from_stretches(
        int sprite_size,
        std::span<const hrz_proto::Rangei* const> stretches)
    {
        cuts.clear();

        insert_cut(0, false);
        for (const auto& stretch : stretches)
        {
            if (stretch->size() <= 0) continue;

            if (insert_cut(stretch->offset(), true))
            {
                insert_cut(stretch->offset() + stretch->size(), false);
            }
        }

        // If there is still only 1 cut, we force it (which covers the whole image) to be
        // stretchable, otherwise the image can't be stretched at all.
        assert(!cuts.empty());
        if (cuts.size() == 1)
        {
            cuts[0].stretch = true;
        }
        insert_cut(sprite_size, false, false);
    }

    // Iterate on the cuts and returns via the callback the position of the cut, its width (distance
    // to last cut) and whether the region between it and the last cut was stretchy or not.
    void iterate_cuts(const std::function<void(int p0, int p1, bool stretch)>& callback) const
    {
        assert(cuts.size() >= 2);

        callback(0, 0, 0);

        size_t region_count = cuts.size() - 1;
        for (size_t i = 0; i < region_count; ++i)
        {
            const auto& c0 = cuts[i];
            const auto& c1 = cuts[i + 1];
            callback(c0.pos, c1.pos, c0.stretch);
        }
    }

    // First is fixed size, second is stretchy
    std::pair<int, int> sum_within_range(int range0, int range1)
    {
        std::pair<int, int> sum{};
        iterate_cuts(
            [&](int p0, int p1, bool stretch)
            {
                int size_in_range = hrz::clamp(range1, p0, p1) - hrz::clamp(range0, p0, p1);
                if (!stretch)
                {
                    sum.first += size_in_range;
                }
                else
                {
                    sum.second += size_in_range;
                }
            });
        return sum;
    }
};

ElementSystem::PrototypeH ImageElementSystem::make_prototype(
    const hrz_proto::SymbolElement& element_descriptor,
    uint64_t layer_id,
    uint32_t z_index,
    const std::function<
        uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
        register_prp,
    const std::function<uint32_t(const hrz_proto::SymbolElement&)>&)
{
    assert(element_descriptor.type() == ElementType);

    const auto& descriptor = element_descriptor.image();

    Prototype prototype;
    prototype.layer_id = layer_id;
    prototype.z_index = z_index;
    prototype.blend_mode = descriptor.color_blend_mode();
    prototype.blend_strength = descriptor.color_blend_strength();

    prototype.image_url = descriptor.url();
    prototype.image_headers = descriptor.http_headers();
    prototype.image = 0;

    prototype.baking_params.fit_axes = descriptor.fit_axes();
    prototype.baking_params.fit_mode = descriptor.fit_mode();
    prototype.baking_params.image_size = {0, 0};
    prototype.baking_params.default_color_srgb =
        hrz::convert_proto_color_to_bytes(descriptor.color().default_value());
    prototype.baking_params.color_prp = register_prp(
        descriptor.color().name(),
        vector_data::attr_from_color<vector_data::OwnedAttributeValue>(
            prototype.baking_params.default_color_srgb));
    prototype.baking_params.default_scale = descriptor.scale().default_value();
    prototype.baking_params.scale_prp =
        register_prp(descriptor.scale().name(), prototype.baking_params.default_scale);
    prototype.baking_params.default_sprite_index = descriptor.sprite_index().default_value();
    prototype.baking_params.sprite_index_prp = register_prp(
        descriptor.sprite_index().name(), (int64_t)prototype.baking_params.default_sprite_index);
    prototype.baking_params.default_sprite_name = descriptor.sprite_name().default_value();
    prototype.baking_params.sprite_name_prp =
        register_prp(descriptor.sprite_name().name(), prototype.baking_params.default_sprite_name);

    auto alignment_x_prp_name = fmt::format("{}_x", descriptor.alignment().name());
    auto alignment_y_prp_name = fmt::format("{}_y", descriptor.alignment().name());
    prototype.baking_params.default_alignment = hrz::to_lm(descriptor.alignment().default_value());
    prototype.baking_params.alignment_prp.x =
        register_prp(alignment_x_prp_name, prototype.baking_params.default_alignment.x);
    prototype.baking_params.alignment_prp.y =
        register_prp(alignment_y_prp_name, prototype.baking_params.default_alignment.y);

    // Bake the sprite geometries
    if (descriptor.sprites_size() > 0)
    {
        prototype.is_sprite = true;

        SpriteCuts cuts_x, cuts_y;
        hrz::InlinedVector<int, 32> sprite_geometry_identity;
        hrz::flat_hash_map<hrz::uint128, int> geometry_identity_hash_to_index;

        for (const auto& sprite : descriptor.sprites())
        {
            // Deduplicate the geometries by computing an identity that is the same for all sprites
            // that can share their vertex & index buffers.
            sprite_geometry_identity.clear();
            sprite_geometry_identity.push_back(sprite.size().x());
            sprite_geometry_identity.push_back(sprite.size().y());
            sprite_geometry_identity.push_back(sprite.stretch_x_size());
            sprite_geometry_identity.push_back(sprite.stretch_y_size());
            for (const auto& s : sprite.stretch_x())
            {
                sprite_geometry_identity.push_back(s.offset());
                sprite_geometry_identity.push_back(s.size());
            }
            for (const auto& s : sprite.stretch_y())
            {
                sprite_geometry_identity.push_back(s.offset());
                sprite_geometry_identity.push_back(s.size());
            }

            hrz::uint128 identity_hash = murmur3_x64_128(std::span<const std::byte>(
                (const std::byte*)sprite_geometry_identity.data(),
                sprite_geometry_identity.size() * 4));

            cuts_x.make_cuts_from_stretches(
                sprite.size().x(), {sprite.stretch_x().data(), (size_t)sprite.stretch_x_size()});

            cuts_y.make_cuts_from_stretches(
                sprite.size().y(), {sprite.stretch_y().data(), (size_t)sprite.stretch_y_size()});

            int geometry_index = 0;

            auto geometry_it = geometry_identity_hash_to_index.find(identity_hash);
            if (geometry_it != geometry_identity_hash_to_index.end())
            {
                geometry_index = geometry_it->second;
            }
            else
            {
                size_t first_vertex = prototype.vertex_buffer_data.size();

                // Generate the vertices
                lm::vec4 pos_fixed_stretchy{};
                cuts_y.iterate_cuts(
                    [&](int y0, int y1, bool stretch_y)
                    {
                        int height = y1 - y0;
                        pos_fixed_stretchy.xy.x = 0;
                        pos_fixed_stretchy.xy.y += stretch_y ? 0 : height;
                        pos_fixed_stretchy.zw.x = 0;
                        pos_fixed_stretchy.zw.y += stretch_y ? height : 0;

                        cuts_x.iterate_cuts(
                            [&](int x0, int x1, bool stretch_x)
                            {
                                int width = x1 - x0;
                                pos_fixed_stretchy.xy.x += stretch_x ? 0 : width;
                                pos_fixed_stretchy.zw.x += stretch_x ? width : 0;

                                // We extend the uv by 0.5 on the sides of the symbol so that we can
                                // then clamp it and thus add a half pixel border with the color of
                                // the border color with a clamp between 0 and 1 in the fragment
                                // shader. This simulates using a single texture with clamp to edges
                                // mode.
                                lm::vec2 uv = {
                                    ((float)x1 - 0.5f) / (float)(sprite.size().x() - 1),
                                    ((float)y1 - 0.5f) / (float)(sprite.size().y() - 1)};

                                prototype.vertex_buffer_data.push_back({pos_fixed_stretchy, uv});
                            });
                    });

                // Generate the triangle strips.
                // 0 --- 1 --- 2 --- 3
                // | 0 / | 2 / | 4 / |
                // | / 1 | / 3 | / 5 |
                // 4 --- 5 --- 6 --- 7
                // | \10 | \ 8 | \ 6 |
                // | 11\ | 9 \ | 7 \ |
                // 8 --- 9 --- 10--- 11

                size_t quad_cols = cuts_x.cuts.size() - 1;
                size_t quad_rows = cuts_y.cuts.size() - 1;
                size_t first_index = prototype.index_buffer_data.size();

                for (int y = 0; y < (int)quad_rows; ++y)
                {
                    int base, x_offset, y_offset;

                    if (y % 2 == 0)
                    {
                        base = y * cuts_x.cuts.size();
                        x_offset = 1;
                    }
                    else
                    {
                        base = (y + 1) * cuts_x.cuts.size() - 1;
                        x_offset = -1;
                    }

                    base += first_vertex;
                    y_offset = cuts_x.cuts.size();

                    prototype.index_buffer_data.push_back((uint16_t)base);
                    prototype.index_buffer_data.push_back((uint16_t)(base + y_offset));

                    for (int x = 0; x < (int)quad_cols; ++x)
                    {
                        prototype.index_buffer_data.push_back(
                            (uint16_t)(base + x_offset * (x + 1)));
                        prototype.index_buffer_data.push_back(
                            (uint16_t)(base + x_offset * (x + 1) + y_offset));
                    }
                }

                size_t index_count = prototype.index_buffer_data.size() - first_index;

                SymbolBakingData::Image::SpriteGeometry sprite_geometry;
                sprite_geometry.first_index = (uint32_t)first_index;
                sprite_geometry.index_count = (uint32_t)index_count;

                geometry_index = (int)prototype.baking_params.sprite_geometries.size();
                prototype.baking_params.sprite_geometries.push_back(sprite_geometry);
                geometry_identity_hash_to_index.insert(
                    std::make_pair(identity_hash, geometry_index));
            }

            SymbolBakingData::Image::Sprite sprite_prototype;
            sprite_prototype.geometry_index = geometry_index;
            sprite_prototype.atlas_size = to_lm(sprite.size());
            sprite_prototype.atlas_offset = to_lm(sprite.offset());

            auto content_box = to_lm(sprite.content());
            if (lm::any(lm::size(content_box) <= 0))
            {
                content_box.min = lm::ivec2{0, 0};
                content_box.max = sprite_prototype.atlas_size;
            }

            std::pair<int, int> content_offset_x = cuts_x.sum_within_range(0, content_box.min.x);
            std::pair<int, int> content_size_x =
                cuts_x.sum_within_range(content_box.min.x, content_box.max.x);

            std::pair<int, int> content_offset_y = cuts_y.sum_within_range(0, content_box.min.y);
            std::pair<int, int> content_size_y =
                cuts_y.sum_within_range(content_box.min.y, content_box.max.y);

            std::pair<int, int> full_size_x =
                cuts_x.sum_within_range(0, sprite_prototype.atlas_size.x);
            std::pair<int, int> full_size_y =
                cuts_y.sum_within_range(0, sprite_prototype.atlas_size.y);

            sprite_prototype.content_offset_fixed.x = content_offset_x.first;
            sprite_prototype.content_offset_fixed.y = content_offset_y.first;
            sprite_prototype.content_offset_stretch.x = content_offset_x.second;
            sprite_prototype.content_offset_stretch.y = content_offset_y.second;

            sprite_prototype.content_size_fixed.x = content_size_x.first;
            sprite_prototype.content_size_fixed.y = content_size_y.first;
            sprite_prototype.content_size_stretch.x = content_size_x.second;
            sprite_prototype.content_size_stretch.y = content_size_y.second;

            sprite_prototype.full_size_fixed.x = full_size_x.first;
            sprite_prototype.full_size_fixed.y = full_size_y.first;
            sprite_prototype.full_size_stretch.x = full_size_x.second;
            sprite_prototype.full_size_stretch.y = full_size_y.second;

            prototype.baking_params.sprites.push_back(sprite_prototype);

            if (!sprite.name().empty())
            {
                prototype.baking_params.sprite_name_to_index.insert(std::make_pair(
                    sprite.name(), (int)(prototype.baking_params.sprites.size() - 1)));
            }
        }
    }
    else // No sprite, generate a dummy one
    {
        prototype.is_sprite = false;

        prototype.vertex_buffer_data.push_back({{0, 0, 0, 0}, {0, 0}});
        prototype.vertex_buffer_data.push_back({{0, 0, 1, 0}, {1, 0}});
        prototype.vertex_buffer_data.push_back({{0, 0, 0, 1}, {0, 1}});
        prototype.vertex_buffer_data.push_back({{0, 0, 1, 1}, {1, 1}});

        prototype.index_buffer_data.push_back(0);
        prototype.index_buffer_data.push_back(2);
        prototype.index_buffer_data.push_back(1);
        prototype.index_buffer_data.push_back(3);

        SymbolBakingData::Image::SpriteGeometry sprite_geometry;
        sprite_geometry.first_index = 0;
        sprite_geometry.index_count = 4;

        SymbolBakingData::Image::Sprite sprite_prototype;
        sprite_prototype.geometry_index = -1; // We use this as a marker that this needs to be
                                              // generated once we know the size of the image.

        prototype.baking_params.sprite_geometries.push_back(sprite_geometry);
        prototype.baking_params.sprites.push_back(sprite_prototype);
    }

    prototype.status = Prototype::Status::WaitingForImage;

    auto handle = _prototypes.alloc(std::move(prototype));

    _loading_prototypes.insert(handle);

    return {ElementType, handle};
}

void ImageElementSystem::unregister_properties(
    PrototypeH prototype_handle,
    const std::function<void(uint64_t prp_id)>& unregister_property) const
{
    if (prototype_handle.type != ElementType) return;

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype)
    {
        unregister_property(prototype->baking_params.color_prp);
        unregister_property(prototype->baking_params.scale_prp);
        unregister_property(prototype->baking_params.sprite_index_prp);
    }
}

void ImageElementSystem::delete_prototype(PrototypeH prototype_handle)
{
    if (prototype_handle.type != ElementType) return;

    if (_prototypes.is_valid(prototype_handle.handle))
    {
        _loading_prototypes.erase(prototype_handle.handle);
        _deleted_prototypes.insert(prototype_handle.handle);
    }
}

ElementSystem::PrototypeStatus ImageElementSystem::get_prototype_status(
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
        case Prototype::Status::WaitingForImage:
        case Prototype::Status::Uploading: return PrototypeStatus::Loading;
        case Prototype::Status::Ready: return PrototypeStatus::Ready;
        default: return PrototypeStatus::Error;
    }
}

SymbolBakingData::ElementBakingParams ImageElementSystem::get_prototype_baking_params(
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

std::optional<ElementSystem::RenderableH> ImageElementSystem::make_renderable(
    PrototypeH prototype_handle,
    uint64_t layer_id,
    TileCoords tile_coords,
    BakedSymbols::ElementInstances&& baked_instances_generic,
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
    if (prototype_handle.type != ElementType || baked_instances_generic.type != ElementType)
    {
        assert(false && "Unexpected element type");
        HRZ_LOG_ERROR(
            "Unexpected element type: expect {}, got {}",
            hrz_proto::SymbolElementType_Name(ElementType),
            hrz_proto::SymbolElementType_Name(baked_instances_generic.type));
        return std::nullopt;
    }

    auto prototype = _prototypes.get_object(prototype_handle.handle);
    if (prototype == nullptr)
    {
        HRZ_LOG_ERROR("Could not find prototype");
        return std::nullopt;
    }

    if (prototype->image_texture.is_null())
    {
        HRZ_LOG_ERROR("Image not loaded");
        return std::nullopt;
    }

    auto tile_coords_str = fmt::to_string(tile_coords);

    auto baked_data =
        std::move(std::get<BakedSymbols::ImageInstances>(baked_instances_generic.data));
    auto instance_data = baked_data.instances.get_data();

    my::BufferResource vib_res(my::BufferResource::BufferType::Vertex);
    vib_res.size = instance_data.size_bytes();
    vib_res.usage = my::UsageHint::Static;
    vib_res.data = (void*)instance_data.data();
    vib_res.allow_allocation_failure = true;

    auto instance_data_buffer = render->rc->alloc(
        &vib_res, hrz::monitoring::systems::Symbols, layer_id,
        {{"contents"_ss, "image instance data"_ss}, {"tile coords"_ss, tile_coords_str}});

    using ImageInstance = BakedSymbols::ImageInstance;

    hrz::InlinedVector<ImageRenderable::Batch, 16> batches;

    for (const auto& batch_info : baked_data.batches)
    {
        size_t instance_byte_offset = sizeof(ImageInstance) * batch_info.first_instance;

        my::VertexInputStream streams[] = {
            {InPosFixedStretchyInputStream, prototype->vertex_buffer, my::VertexFormat::Float32_4,
             offsetof(Prototype::Vertex, pos_fixed_stretchy), sizeof(Prototype::Vertex),
             my::VertexRate::PerVertex},
            {UvInputStream, prototype->vertex_buffer, my::VertexFormat::Float32_2,
             offsetof(Prototype::Vertex, uv), sizeof(Prototype::Vertex), my::VertexRate::PerVertex},
            {TransformInputStream + 0, instance_data_buffer, my::VertexFormat::Float32_4,
             instance_byte_offset + offsetof(ImageInstance, transform) + sizeof(lm::vec4) * 0,
             sizeof(ImageInstance), my::VertexRate::PerInstance},
            {TransformInputStream + 1, instance_data_buffer, my::VertexFormat::Float32_4,
             instance_byte_offset + offsetof(ImageInstance, transform) + sizeof(lm::vec4) * 1,
             sizeof(ImageInstance), my::VertexRate::PerInstance},
            {TransformInputStream + 2, instance_data_buffer, my::VertexFormat::Float32_4,
             instance_byte_offset + offsetof(ImageInstance, transform) + sizeof(lm::vec4) * 2,
             sizeof(ImageInstance), my::VertexRate::PerInstance},
            {TransformInputStream + 3, instance_data_buffer, my::VertexFormat::Float32_4,
             instance_byte_offset + offsetof(ImageInstance, transform) + sizeof(lm::vec4) * 3,
             sizeof(ImageInstance), my::VertexRate::PerInstance},
            {StretchSizeInputStream, instance_data_buffer, my::VertexFormat::Float32_2,
             instance_byte_offset + offsetof(ImageInstance, stretch_size), sizeof(ImageInstance),
             my::VertexRate::PerInstance},
            {ColorInputStream, instance_data_buffer, my::VertexFormat::UInt8Norm_4,
             instance_byte_offset + offsetof(ImageInstance, color), sizeof(ImageInstance),
             my::VertexRate::PerInstance},
            {AnchorIndexInputStream, instance_data_buffer, my::VertexFormat::UInt32,
             instance_byte_offset + offsetof(ImageInstance, anchor_index), sizeof(ImageInstance),
             my::VertexRate::PerInstance},
            {UvOffsetInputStream, instance_data_buffer, my::VertexFormat::Float32_2,
             instance_byte_offset + offsetof(ImageInstance, uv_offset), sizeof(ImageInstance),
             my::VertexRate::PerInstance},
            {UvSizeInputStream, instance_data_buffer, my::VertexFormat::Float32_2,
             instance_byte_offset + offsetof(ImageInstance, uv_size), sizeof(ImageInstance),
             my::VertexRate::PerInstance}};

        my::VertexInputResource vi_res;
        vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
        vi_res.attribs = streams;
        vi_res.indices = prototype->index_buffer;

        ImageRenderable::Batch batch;
        batch.first_index = batch_info.first_index;
        batch.index_count = batch_info.index_count;
        batch.instance_count = batch_info.instance_count;
        batch.vertex_input = render->rc->alloc(
            &vi_res, hrz::monitoring::systems::Symbols, layer_id,
            {{"image vertex input"_ss, tile_coords_str}});

        batches.push_back(batch);
    }

    // ImageRenderable is self-referential (data.batches points to batches), so we need to construct
    // it in place so that its data is not moved.

    auto handle = _renderables.alloc(ImageRenderable{});
    ImageRenderable* renderable = _renderables.get_object(handle);

    renderable->center = bsphere_center;
    renderable->radius = bsphere_radius;
    renderable->z_index = z_index & ~0xff;

    renderable->instance_data_buffer = instance_data_buffer;
    renderable->batches = std::move(batches);

    renderable->data.batches = renderable->batches;
    renderable->data.anchor_ubo = anchor_ubo;
    renderable->data.anchor_data_texture = anchor_data_texture;
    renderable->data.selection_texture = selection_texture;
    renderable->data.culling_visibility_textures = culling_visibility_textures;
    renderable->data.data_texture_sampler = anchor_data_texture_sampler;
    renderable->data.image_texture = prototype->image_texture;
    renderable->data.image_texture_sampler =
        prototype->is_sprite ? _sprite_image_sampler : _simple_image_sampler;
    renderable->data.visual_shader = _visual_shader;
    renderable->data.picking_shader = _picking_shader;
    renderable->data.selection_shader = _selection_shader;
    renderable->data.tile_ubo = tile_ubo;
    renderable->data.image_ubo = prototype->ubo;

    return {{ElementType, handle}};
}

void ImageElementSystem::delete_renderable(RenderableH renderable_handle)
{
    if (renderable_handle.type != ElementType) return;

    auto renderable = _renderables.get_object(renderable_handle.handle);
    if (renderable)
    {
        _unused_resources.push_back(renderable->instance_data_buffer);
        for (const auto& batch : renderable->batches)
        {
            _unused_resources.push_back(batch.vertex_input);
        }

        _renderables.release(renderable_handle.handle);
    }
}

void ImageElementSystem::draw_renderable(
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

void ImageElementSystem::work(ReprSystem::WorkCtx& ctx)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol image work");

    for (auto handle : _deleted_prototypes)
    {
        auto prototype = _prototypes.get_object(handle);
        if (!prototype) continue;

        if (image_loader::is_image_valid(ctx.il, prototype->image))
        {
            image_loader::release_image(ctx.il, prototype->image);
        }

        switch (prototype->status)
        {
            case Prototype::Status::Ready:
            case Prototype::Status::Error:
                if (!prototype->ubo.is_null())
                {
                    _unused_resources.push_back(prototype->ubo);
                }
                if (!prototype->vertex_buffer.is_null())
                {
                    _unused_resources.push_back(prototype->vertex_buffer);
                }
                if (!prototype->index_buffer.is_null())
                {
                    _unused_resources.push_back(prototype->index_buffer);
                }
                break;
            default: break;
        }

        _prototypes.release(handle);
    }
    _deleted_prototypes.clear();

    for (auto it = _loading_prototypes.begin(); it != _loading_prototypes.end();)
    {
        auto prototype = _prototypes.get_object(*it);
        bool erase = false;

        if (!prototype)
        {
            _loading_prototypes.erase(it++);
            continue;
        }

        if (prototype->status == Prototype::Status::WaitingForImage)
        {
            if (!image_loader::is_image_valid(ctx.il, prototype->image))
            {
                prototype->image = image_loader::load_image(
                    ctx.il, prototype->image_url, prototype->image_headers,
                    {monitoring::systems::Symbols, prototype->layer_id});
            }
            else
            {
                auto image_status = image_loader::get_image_status(ctx.il, prototype->image);

                if (image_status == image_loader::ImageStatus::Error)
                {
                    HRZ_LOG_ERROR("Could not load image");
                    image_loader::release_image(ctx.il, prototype->image);
                    prototype->status = Prototype::Status::Error;
                    erase = true;
                }
                else if (image_status == image_loader::ImageStatus::Loaded)
                {
                    auto image = image_loader::get_image_texture(ctx.il, prototype->image);
                    prototype->image_texture = image.texture;
                    prototype->baking_params.image_size = {
                        (int32_t)image.size.x, (int32_t)image.size.y};

                    for (auto& sprite : prototype->baking_params.sprites)
                    {
                        // When we generate a default sprite, we can't know in advance the
                        // size of the image, so we put -1 and fix it here.
                        if (sprite.geometry_index < 0)
                        {
                            sprite.atlas_offset = {};
                            sprite.atlas_size = prototype->baking_params.image_size;

                            sprite.content_offset_fixed = {};
                            sprite.content_offset_stretch = {};

                            sprite.content_size_fixed = {};
                            sprite.content_size_stretch = sprite.atlas_size;

                            sprite.full_size_fixed = {};
                            sprite.full_size_stretch = sprite.atlas_size;

                            sprite.geometry_index = 0;

                            prototype->vertex_buffer_data[1].pos_fixed_stretchy.z =
                                sprite.content_size_stretch.x;
                            prototype->vertex_buffer_data[2].pos_fixed_stretchy.w =
                                sprite.content_size_stretch.y;
                            prototype->vertex_buffer_data[3].pos_fixed_stretchy.z =
                                sprite.content_size_stretch.x;
                            prototype->vertex_buffer_data[3].pos_fixed_stretchy.w =
                                sprite.content_size_stretch.y;
                        }
                    }

                    prototype->status = Prototype::Status::Uploading;
                }
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

void ImageElementSystem::work_gpu(Render* render)
{
    HRZ_SCOPED_SAMPLE("vector repr symbol image work gpu");

    for (auto resource : _unused_resources)
    {
        render->rc->dealloc(resource);
    }
    _unused_resources.clear();

    for (auto it = _loading_prototypes.begin(); it != _loading_prototypes.end();)
    {
        auto prototype = _prototypes.get_object(*it);
        bool erase = false;

        if (!prototype)
        {
            _loading_prototypes.erase(it++);
            continue;
        }

        if (prototype->status == Prototype::Status::Uploading)
        {
            {
                ImageUniformData ubo;
                ubo.z_index = prototype->z_index;
                ubo.blend_mode = prototype->blend_mode;
                ubo.blend_strength = prototype->blend_strength;

                my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
                ub_res.size = sizeof(ubo);
                ub_res.usage = my::UsageHint::Static;
                ub_res.data = &ubo;
                prototype->ubo = render->rc->alloc(
                    &ub_res, hrz::monitoring::systems::Symbols, prototype->layer_id);
            }

            {
                my::BufferResource ib_res(my::BufferResource::BufferType::Index);
                ib_res.size = prototype->index_buffer_data.size() * sizeof(uint16_t);
                ib_res.usage = my::UsageHint::Static;
                ib_res.data = (void*)prototype->index_buffer_data.data();

                prototype->index_buffer = render->rc->alloc(
                    &ib_res, hrz::monitoring::systems::Symbols,
                    {{"contents"_ss, "image index data"_ss}});
            }

            {
                my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
                vb_res.size = prototype->vertex_buffer_data.size() * sizeof(Prototype::Vertex);
                vb_res.usage = my::UsageHint::Static;
                vb_res.data = prototype->vertex_buffer_data.data();

                prototype->vertex_buffer = render->rc->alloc(
                    &vb_res, hrz::monitoring::systems::Symbols,
                    {{"contents"_ss, "image vertex data"_ss}});
            }

            prototype->vertex_buffer_data.clear();
            prototype->vertex_buffer_data.shrink_to_fit();

            prototype->index_buffer_data.clear();
            prototype->index_buffer_data.shrink_to_fit();

            prototype->status = prototype->image_texture.is_null() || prototype->ubo.is_null()
                ? Prototype::Status::Error
                : Prototype::Status::Ready;
            erase = true;
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
} // namespace hrz::vt::symbol
