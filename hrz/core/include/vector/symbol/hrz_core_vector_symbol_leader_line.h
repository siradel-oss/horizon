#pragma once

#include "vector/symbol/hrz_core_vector_symbol_element.h"

#include <hrz_fnd_gen_object_pool.h>

namespace hrz::vt::symbol
{
struct LeaderLineRenderable : public my::Renderer::Renderable
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
        my::ResourceHandle leader_line_ubo = my::ResourceHandle::null();

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

struct LeaderLineElementSystem : public ElementSystem
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

        SymbolBakingData::LeaderLine baking_params;
    };

    static constexpr hrz_proto::SymbolElementType ElementType =
        hrz_proto::SymbolElementType::LEADER_LINE_SYMBOL_ELEMENT;

    using PrototypeIndexPool = hrz::GenIndexPool<RawPrototypeH, 32, 32>;
    using PrototypePool = hrz::GenObjectPool<Prototype, PrototypeIndexPool, 64>;

    using RenderableIndexPool = hrz::GenIndexPool<RawRenderableH, 32, 32>;
    using RenderablePool = hrz::GenObjectPool<LeaderLineRenderable, RenderableIndexPool, 64>;

    PrototypePool _prototypes;
    RenderablePool _renderables;

    hrz::flat_hash_set<RawPrototypeH> _loading_prototypes;
    hrz::flat_hash_set<RawPrototypeH> _deleted_prototypes;

    my::ResourceHandle _visual_shader = my::ResourceHandle::null();
    my::ResourceHandle _picking_shader = my::ResourceHandle::null();
    my::ResourceHandle _selection_shader = my::ResourceHandle::null();
    my::ResourceHandle _vertex_data_buffer = my::ResourceHandle::null();

    std::vector<my::ResourceHandle> _unused_resources;

    bool is_visual() const override { return true; }

    void deinit(ReprSystem::WorkCtx&, Render*) override {}

    void init_render(Render*) override;
    void deinit_render(Render*) override;

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

    void delete_prototype(PrototypeH) override;

    PrototypeStatus get_prototype_status(PrototypeH prototype_handle) const override;

    SymbolBakingData::ElementBakingParams get_prototype_baking_params(
        PrototypeH prototype_handle) const override
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

    std::optional<RenderableH> make_renderable(
        PrototypeH,
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
        my::ResourceHandle data_texture_sampler,
        uint32_t z_index,
        Render*) override;

    void delete_renderable(RenderableH) override;

    void draw_renderable(
        RenderableH,
        uint32_t scene_views,
        bool has_any_selected,
        bool ignore_occlusions,
        Render*) override;

    void work(ReprSystem::WorkCtx&) override;

    void work_gpu(Render*) override;

public:
    static void collect_shaders(hrz::GpuResourceContext* rc);
};
} // namespace hrz::vt::symbol
