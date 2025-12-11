#pragma once

#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/blob_image.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/vector/image_loader.h"
#include "hrz/core/vector/symbol/element.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_object_pool.h"

#include <mycelium/renderer.h>

namespace hrz::vt::symbol
{
struct ImageRenderable : public my::Renderer::Renderable
{
    lm::dvec3 center;
    double radius;

    uint32_t z_index;

    struct Batch
    {
        uint32_t first_index;
        uint32_t index_count;
        uint32_t instance_count;
        my::ResourceHandle vertex_input;
    };

    hrz::InlinedVector<Batch, 16> batches;
    my::ResourceHandle instance_data_buffer = my::ResourceHandle::null();

    struct RenderData
    {
        std::span<Batch> batches;

        my::ResourceHandle anchor_ubo = my::ResourceHandle::null();
        my::ResourceHandle anchor_data_texture = my::ResourceHandle::null();
        my::ResourceHandle selection_texture = my::ResourceHandle::null();
        std::array<my::ResourceHandle, SCENE_VIEW_COUNT> culling_visibility_textures{};
        my::ResourceHandle data_texture_sampler = my::ResourceHandle::null();

        my::ResourceHandle image_texture = my::ResourceHandle::null();
        my::ResourceHandle image_texture_sampler = my::ResourceHandle::null();

        my::ResourceHandle visual_shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();
        my::ResourceHandle tile_ubo = my::ResourceHandle::null();
        my::ResourceHandle image_ubo = my::ResourceHandle::null();

        uint32_t scene_views;
        bool has_any_selected;
        bool ignore_occlusions;
    } data;

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

struct ImageElementSystem : public ElementSystem
{
private:
    struct Prototype
    {
        enum class Status
        {
            WaitingForImage,
            Uploading,
            Ready,
            Error,
        };

        struct Vertex
        {
            // xy are xy fixed
            // zw are xy stretchy
            // A vertex position is xy + zw * stretch_size
            lm::vec4 pos_fixed_stretchy;
            lm::vec2 uv;
        };

        Status status;
        uint64_t layer_id;
        uint32_t z_index;

        std::string image_url;
        hrz_proto::HttpHeaderList image_headers;
        image_loader::ImageH image = 0;
        my::ResourceHandle image_texture = my::ResourceHandle::null();
        bool is_sprite = false;

        uint32_t blend_mode = 0;
        float blend_strength = 0;

        std::vector<uint16_t> index_buffer_data;
        std::vector<Vertex> vertex_buffer_data;
        my::ResourceHandle vertex_buffer = my::ResourceHandle::null();
        my::ResourceHandle index_buffer = my::ResourceHandle::null();

        my::ResourceHandle ubo = my::ResourceHandle::null();

        hrz_jobs::SymbolBakingData::Image baking_params;
    };

    static constexpr hrz_proto::SymbolElementType ElementType =
        hrz_proto::SymbolElementType::IMAGE_SYMBOL_ELEMENT;

    using PrototypeIndexPool = hrz::GenIndexPool<RawPrototypeH, 32, 32>;
    using PrototypePool = hrz::GenObjectPool<Prototype, PrototypeIndexPool, 64>;

    using RenderableIndexPool = hrz::GenIndexPool<RawRenderableH, 32, 32>;
    using RenderablePool = hrz::GenObjectPool<ImageRenderable, RenderableIndexPool, 64>;

    PrototypePool _prototypes;
    RenderablePool _renderables;

    hrz::flat_hash_set<RawPrototypeH> _loading_prototypes;
    hrz::flat_hash_set<RawPrototypeH> _deleted_prototypes;

    my::ResourceHandle _visual_shader = my::ResourceHandle::null();
    my::ResourceHandle _picking_shader = my::ResourceHandle::null();
    my::ResourceHandle _selection_shader = my::ResourceHandle::null();
    my::ResourceHandle _simple_image_sampler = my::ResourceHandle::null();
    my::ResourceHandle _sprite_image_sampler = my::ResourceHandle::null();

    std::vector<my::ResourceHandle> _unused_resources;

public:
    static void collect_shaders(hrz::GpuResourceContext* rc);

    bool is_visual() const override { return true; }

    void deinit(ReprSystem::WorkCtx& ctx, Render* render) override {}

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
};
} // namespace hrz::vt::symbol
