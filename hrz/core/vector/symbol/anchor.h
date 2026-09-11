// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/vector/symbol/element.h"
#include "hrz/fnd/gen_object_pool.h"

namespace hrz::vt::symbol
{

struct AnchorElementSystem : public ElementSystem
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

        hrz::vt::AnchorPrototype info;

        my::ResourceHandle ubo = my::ResourceHandle::null();

        hrz_jobs::SymbolBakingData::Anchor baking_params;
    };

    static constexpr hrz_proto::SymbolElement::ElementTypeCase ElementType =
        hrz_proto::SymbolElement::ElementTypeCase::kAnchor;

    using PrototypeIndexPool = hrz::GenIndexPool<RawPrototypeH, 32, 32>;
    using PrototypePool = hrz::GenObjectPool<Prototype, PrototypeIndexPool, 64>;

    PrototypePool _prototypes;

    hrz::flat_hash_set<RawPrototypeH> _loading_prototypes;
    hrz::flat_hash_set<RawPrototypeH> _deleted_prototypes;

    std::vector<my::ResourceHandle> _unused_resources;

public:
    bool is_visual() const override { return false; }

    void deinit(ReprSystem::WorkCtx&, Render*) override {}

    void init_render(Render*) override {}

    void deinit_render(Render*) override {}

    PrototypeH make_prototype(
        const hrz_proto::SymbolElement& element_descriptor,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
        >& register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype)
        override;

    void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const override;

    void delete_prototype(PrototypeH) override;

    PrototypeStatus get_prototype_status(PrototypeH prototype_handle) const override
    {
        if (prototype_handle.type == ElementType
            && _prototypes.get_object(prototype_handle.handle) != nullptr)
        {
            return PrototypeStatus::Ready;
        }

        return PrototypeStatus::Error;
    }

    hrz_jobs::SymbolBakingData::ElementBakingParams get_prototype_baking_params(
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

    my::ResourceHandle get_prototype_ubo(PrototypeH prototype_handle)
    {
        if (prototype_handle.type != ElementType)
        {
            assert(false);
            return my::ResourceHandle::null();
        }

        auto prototype = _prototypes.get_object(prototype_handle.handle);
        if (prototype == nullptr)
        {
            assert(false);
            return my::ResourceHandle::null();
        }

        if (prototype->status != Prototype::Status::Ready)
        {
            assert(false);
            return my::ResourceHandle::null();
        }

        return {prototype->ubo};
    }

    std::optional<vt::AnchorPrototype> get_anchor_prototype(PrototypeH handle) const override;

    std::optional<RenderableH> make_renderable(
        PrototypeH,
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
        Render*) override
    {
        return std::nullopt;
    }

    void delete_renderable(RenderableH) override {}

    void draw_renderable(
        RenderableH,
        uint32_t scene_views,
        bool has_any_selected,
        bool ingore_occlusions,
        Render*) override
    {
    }

    void work(ReprSystem::WorkCtx&) override;

    void work_gpu(Render*) override;
};

} // namespace hrz::vt::symbol
