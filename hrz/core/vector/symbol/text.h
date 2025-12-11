#pragma once

#include "hrz/common/font_rasterizer.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/vector/symbol/element.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/http.h"

#include <mycelium/renderer.h>

#include <utility>

namespace hrz::vt::symbol
{
struct FontReference
{
    std::string url;
    hrz::HttpHeaders headers;

    bool operator==(const FontReference& r) const
    {
        return headers.hash_content() == r.headers.hash_content() && url == r.url;
    }

    template<typename H>
    friend H AbslHashValue(H h, const FontReference& ref)
    {
        return H::combine(std::move(h), ref.url, ref.headers);
    }
};

struct TextRenderable : public my::Renderer::Renderable
{
    // If non-indexed, non-instanced rendering was used, many vertices would have
    // to be duplicated.
    //
    // If indexed rendering was used to draw glyphs, a completely deterministic
    // (and therefore containing no actual information) index buffer would be
    // required. (Containing [0, 1, 2, 0, 2, 3, 0, 1, 2, ...].)
    //
    // Instanced rendering is used, with one instance per glyph. However vertex
    // data containing positions and UVs must not be restart at the beginning
    // between two instances. It is not possible to specify this. To counter
    // this, vertex data for each of the four vertices of one glyph are declared
    // as their own attribute, which advances per instance. That makes all the
    // vertex data for a glyph available at the same time, but most importantly
    // it allows progressing through the vertex data for each instance (each
    // glyph).

    lm::dvec3 center;
    double radius;

    uint32_t z_index;

    my::ResourceHandle pos_uv_buffer;
    my::ResourceHandle text_index_buffer;

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();

        my::ResourceHandle anchor_ubo = my::ResourceHandle::null();
        my::ResourceHandle anchor_data_texture = my::ResourceHandle::null();
        my::ResourceHandle selection_texture = my::ResourceHandle::null();
        std::array<my::ResourceHandle, SCENE_VIEW_COUNT> culling_visibility_textures{};
        my::ResourceHandle anchor_data_texture_sampler = my::ResourceHandle::null();

        my::ResourceHandle font_texture = my::ResourceHandle::null();
        my::ResourceHandle font_texture_sampler = my::ResourceHandle::null();

        my::ResourceHandle anchor_index_texture = my::ResourceHandle::null();
        my::ResourceHandle transform_texture = my::ResourceHandle::null();
        my::ResourceHandle outline_width_texture = my::ResourceHandle::null();
        my::ResourceHandle fill_color_texture = my::ResourceHandle::null();
        my::ResourceHandle outline_color_texture = my::ResourceHandle::null();
        my::ResourceHandle text_data_texture_sampler = my::ResourceHandle::null();

        my::ResourceHandle fill_visual_shader = my::ResourceHandle::null();
        my::ResourceHandle outline_visual_shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();

        my::ResourceHandle tile_ubo = my::ResourceHandle::null();
        my::ResourceHandle text_ubo = my::ResourceHandle::null();

        uint32_t instance_count = 0;
        uint32_t scene_views;
        bool has_any_selected;
        bool ignore_occlusions;

        bool has_non_zero_outline_width;
    } data;

    // The outlines are rendered first. They are not sorted by depth,
    // so there can be artifacts on their outer borders (which is
    // antialiased) when texts are overlapping. But it's either that
    // or hard edges, which are bad looking all the time.
    // The texts themselves are drawn on top of the outlines, so their
    // antialiased border should always be correct.

    // Some glyphs must be graphically connected one to another is order
    // for the rendering to be correct. This is the case for example for
    // the Arabic script.
    // If each glyph's outline was rendered at the same time as the
    // glyph itself, where glyphs should connect, the outline would be
    // drawn over the previous glyph instead.
    // Instead by drawing with two passes, the glyphs can be neatly drawn
    // over the outline of the whole text.

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw_data);

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        auto bin = data.ignore_occlusions ? hrz::RenderSymbolicOverlayBin : hrz::RenderSymbolicBin;

        if (culler.is_visible_in_any_view(center, radius, bin))
        {
            queue.enqueue(bin, render_callback, &data, center, radius, z_index);
        }
    }
};

struct TextElementSystem : public ElementSystem
{
private:
    static constexpr hrz_proto::SymbolElementType ElementType =
        hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT;

    struct Font
    {
        enum class Status
        {
            New,
            Loading,
            Allocating,
            Uploading,
            Loaded,
            Error
        };

        Status status;
        std::string url;
        uint32_t ref_count;
        hrz::assets_loader::Ticket load_ticket;
        font_rasterizer::FontHandle rasterizer_handle;
        hrz::blobs::AllocationTicket blob_ticket;
        hrz::blobs::BlobHandle blob;
        my::ResourceHandle texture;

        std::vector<font_rasterizer::RasterizedGlyph> new_glyphs;
    };

    using FontH = uint16_t;

    using FontIndexPool = hrz::GenIndexPool<FontH, 4, 12>;
    using FontPool = hrz::GenObjectPool<Font, FontIndexPool, 8>;

    FontPool _fonts;
    hrz::flat_hash_map<FontReference, FontH> _fonts_to_handles;

    struct Prototype
    {
        enum class Status
        {
            WaitingForFont,
            Uploading,
            Ready,
            Error,
        };

        Status status;
        uint64_t layer_id;
        uint32_t z_index;

        FontH font = 0;

        my::ResourceHandle ubo = my::ResourceHandle::null();

        hrz_jobs::SymbolBakingData::Text baking_params;
    };

    using PrototypeIndexPool = hrz::GenIndexPool<RawPrototypeH, 32, 32>;
    using PrototypePool = hrz::GenObjectPool<Prototype, PrototypeIndexPool, 64>;

    using RenderableIndexPool = hrz::GenIndexPool<RawRenderableH, 32, 32>;
    using RenderablePool = hrz::GenObjectPool<TextRenderable, RenderableIndexPool, 64>;

    PrototypePool _prototypes;
    RenderablePool _renderables;

    hrz::flat_hash_set<RawPrototypeH> _loading_prototypes;
    hrz::flat_hash_set<RawPrototypeH> _deleted_prototypes;

    my::ResourceHandle _fill_visual_shader = my::ResourceHandle::null();
    my::ResourceHandle _outline_visual_shader = my::ResourceHandle::null();
    my::ResourceHandle _picking_shader = my::ResourceHandle::null();
    my::ResourceHandle _selection_shader = my::ResourceHandle::null();
    my::ResourceHandle _vertex_id_buffer = my::ResourceHandle::null();
    my::ResourceHandle _data_texture_sampler = my::ResourceHandle::null();
    my::ResourceHandle _font_texture_sampler = my::ResourceHandle::null();

    std::vector<my::ResourceHandle> _unused_resources;

public:
    static void collect_shaders(GpuResourceContext* rc);

    bool is_visual() const override { return true; }

    void deinit(ReprSystem::WorkCtx& ctx, Render* render) override;

    void init_render(Render* render) override;

    void deinit_render(Render* render) override;

    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)>&
            register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;

    void delete_prototype(PrototypeH prototype_handle) override;

    PrototypeStatus get_prototype_status(PrototypeH prototype_handle) const override;

    hrz_jobs::SymbolBakingData::ElementBakingParams get_prototype_baking_params(
        PrototypeH prototype_handle) const override;

    std::optional<RenderableH> make_renderable(
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
        my::ResourceHandle anchor_data_texture_sampler,
        uint32_t z_index,
        Render* render) override;

    void delete_renderable(RenderableH renderable_handle) override;

    void draw_renderable(
        RenderableH renderable_handle,
        uint32_t scene_views,
        bool has_any_selected,
        bool ignore_occlusions,
        Render* render) override;

    void work(ReprSystem::WorkCtx& ctx) override;

    void work_gpu(Render* render) override;

private:
    FontH ref_font(const std::string& font_url, const hrz_proto::HttpHeaderList& headers);
    void unref_font(FontH handle);
};
} // namespace hrz::vt::symbol
