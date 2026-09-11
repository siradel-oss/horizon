// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/jobs/vector_tiles_jobs_params.h"
#include "hrz/core/render/resources.h"
#include "hrz/core/vector/repr.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace hrz::vt::symbol
{

enum
{
    TileParamsUbo = hrz::UboCustomStart,
    AnchorParamsUbo,
    ElementCustomUboStart,

    AnchorDataTextureSamplerIndex = hrz::SamplerCustomStart,
    SelectionSamplerIndex,
    CullingVisibilitySamplerIndex,
    ElementCustomSamplerStart,
};

struct TileUniformData
{
    lm::vec4 center_low;
    lm::vec4 center_high;

    lm::uvec2 object_reference;
    uint32_t _padding1[2];

    lm::uvec3 feature_reference;
    uint32_t _padding2[1];
};

HRZ_CHECK_UBO_SIZE(TileUniformData);

struct AnchorUniformData
{
    // See flags in hrz_common_vector_tiles.h
    uint32_t flags;
    float reference_distance;
    float min_relative_scale;
    float max_relative_scale;
};

HRZ_CHECK_UBO_SIZE(AnchorUniformData);

// An element system handles everything related to a specific symbol element,
// except for the baking job.
//
// The two objects an element system can create are prototypes and renderables.
//
// All element types can have prototypes. A prototype takes a Protobuf des-
// criptor of the element and prepares everything that is needed for the
// element instances to be baked. This includes extracting the relevant info
// from the Protobuf message, but also for example loading external resources,
// that are common to all instances (such as an image) and turning them into
// usable data (such as a texture). Once they are ready, prototypes also
// provide the baking parameters for the element (see the `SymbolBakingData`
// structure), which includes the styling property indices. An element system
// is responsible for registering any styling property the element may have.
//
// Visual elements, i.e. those whose instances are rendered by the GPU, have
// renderables. A renderable contains all the data required to render all
// the instances of the element contained in one tile. Combined with the
// GPU resources the prototype has loaded, they allow rendering the instances.
struct ElementSystem
{
    using RawPrototypeH = uint64_t;
    using RawRenderableH = uint64_t;

    struct PrototypeH
    {
        hrz_proto::SymbolElement::ElementTypeCase type;
        ElementSystem::RawPrototypeH handle;
    };

    struct RenderableH
    {
        hrz_proto::SymbolElement::ElementTypeCase type;
        ElementSystem::RawRenderableH handle;
    };

    enum class PrototypeStatus
    {
        Loading,
        Ready,
        Error,
    };

    virtual ~ElementSystem() = default;

    // When true, the system can make renderables.
    virtual bool is_visual() const = 0;

    virtual void deinit(ReprSystem::WorkCtx&, Render*) = 0;
    virtual void init_render(Render*) = 0;
    virtual void deinit_render(Render*) = 0;

    // `layer_id` can be used to correctly identify the owning layer
    // when allocating data.
    // `z_index` can be used to decrease z-fighting when rendering
    // the element instances. It isn't meaningful for non-visual
    // elements.
    // `make_child_prototype()` returns the index of the child.
    virtual PrototypeH make_prototype(
        const hrz_proto::SymbolElement&,
        uint64_t layer_id,
        uint32_t z_index,
        const std::function<
            uint64_t(std::string_view name, const hrz::vector_data::OwnedAttributeValue&)
        >& register_prp,
        const std::function<uint32_t(const hrz_proto::SymbolElement&)>& make_child_prototype) = 0;

    virtual void unregister_properties(
        PrototypeH,
        const std::function<void(uint64_t prp_id)>& unregister_property) const = 0;

    virtual void delete_prototype(PrototypeH) = 0;

    virtual PrototypeStatus get_prototype_status(PrototypeH) const = 0;

    virtual hrz_jobs::SymbolBakingData::ElementBakingParams get_prototype_baking_params(
        PrototypeH) const = 0;

    virtual std::optional<vt::AnchorPrototype> get_anchor_prototype(PrototypeH) const
    {
        return std::nullopt;
    }

    // GPU resources ownership is not transfered.
    // The 8 least significant bits in `z_index` must be 0,
    // as they are used internally by element systems.
    virtual std::optional<RenderableH> make_renderable(
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
        Render*) = 0;
    virtual void delete_renderable(RenderableH) = 0;
    virtual void draw_renderable(
        RenderableH,
        uint32_t scene_views,
        bool has_any_selected,
        bool ignore_occlusions,
        Render*) = 0;

    virtual void work(ReprSystem::WorkCtx&) = 0;
    virtual void work_gpu(Render*) = 0;
};

} // namespace hrz::vt::symbol
