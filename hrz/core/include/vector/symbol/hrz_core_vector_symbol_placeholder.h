#pragma once

#include "vector/symbol/hrz_core_vector_symbol_element.h"

#include <hrz_fnd_gen_object_pool.h>

namespace hrz::vt::symbol
{
// A simple visual element that occupies some area and renders
// it with a simpler colour. Ideal for scaffolding a new symbol
// representation.
struct PlaceholderRenderable : public my::Renderer::Renderable
{
    lm::dvec3 center;
    double radius;

    uint32_t z_index;

    my::ResourceHandle vertex_data_buffer = my::ResourceHandle::null();
    my::ResourceHandle instance_data_buffer = my::ResourceHandle::null();

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();

        my::ResourceHandle anchor_ubo = my::ResourceHandle::null();
        my::ResourceHandle anchor_data_texture = my::ResourceHandle::null();
        my::ResourceHandle selection_texture = my::ResourceHandle::null();
        std::array<my::ResourceHandle, SCENE_VIEW_COUNT> culling_visibility_textures{};
        my::ResourceHandle data_texture_sampler = my::ResourceHandle::null();

        my::ResourceHandle visual_shader = my::ResourceHandle::null();
        my::ResourceHandle picking_shader = my::ResourceHandle::null();
        my::ResourceHandle selection_shader = my::ResourceHandle::null();
        my::ResourceHandle tile_ubo = my::ResourceHandle::null();
        my::ResourceHandle placeholder_ubo = my::ResourceHandle::null();

        uint32_t vertex_count = 0;
        uint32_t instance_count = 1;
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

struct PlaceholderElementSystem : public ElementSystem
{
private:
    struct Prototype
    {
        enum class Status
        {
            Uploading,
            Ready,
            Error,
        };

        Status status;
        uint64_t layer_id;
        uint32_t z_index;

        my::ResourceHandle ubo = my::ResourceHandle::null();

        SymbolBakingData::Placeholder baking_params;
    };

    static constexpr hrz_proto::SymbolElementType ElementType =
        hrz_proto::SymbolElementType::PLACEHOLDER_SYMBOL_ELEMENT;

    using PrototypeIndexPool = hrz::GenIndexPool<RawPrototypeH, 32, 32>;
    using PrototypePool = hrz::GenObjectPool<Prototype, PrototypeIndexPool, 64>;

    using RenderableIndexPool = hrz::GenIndexPool<RawRenderableH, 32, 32>;
    using RenderablePool = hrz::GenObjectPool<PlaceholderRenderable, RenderableIndexPool, 64>;

    PrototypePool _prototypes;
    RenderablePool _renderables;

    hrz::flat_hash_set<RawPrototypeH> _loading_prototypes;
    hrz::flat_hash_set<RawPrototypeH> _deleted_prototypes;

    my::ResourceHandle _visual_shader = my::ResourceHandle::null();
    my::ResourceHandle _picking_shader = my::ResourceHandle::null();
    my::ResourceHandle _selection_shader = my::ResourceHandle::null();
    my::ResourceHandle _vertex_data_buffer = my::ResourceHandle::null();

    std::vector<my::ResourceHandle> _unused_resources;

public:
    static void collect_shaders(hrz::GpuResourceContext* rc);

    bool is_visual() const override { return true; }

    void deinit(ReprSystem::WorkCtx& ctx, Render* render) override {}

    void init_render(Render* render) override;

    void deinit_render(Render* render) override;

    PrototypeH make_prototype(
        const hrz_proto::SymbolElement&,
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

    SymbolBakingData::ElementBakingParams get_prototype_baking_params(PrototypeH) const override;

    std::optional<RenderableH> make_renderable(
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
