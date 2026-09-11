// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/core/model/animation_player.h"
#include "hrz/core/model/geometry.h"
#include "hrz/core/model/instance_group.h"
#include "hrz/core/model/material.h"
#include "hrz/fnd/observed.h"

namespace hrz::model
{

struct ModelPrototype;

// Draw properties that require rebuilding only mesh data
struct MeshDrawProperties
{
    int clip_id = 0;
    lm::vec4 color = {0, 0, 0, 0};
    uint32_t feature_color_blend_mode = 0;
    float feature_color_blend_strength = 0;
    size_t material_revisions[MaterialCount] = {};
    bool overlay_material_enabled = false;
    bool apply_feature_color_to_overlay = false;
    float overlay_material_opacity = 1.0F;
    bool draw_under_flat_overlays = false;

    bool operator !=(const MeshDrawProperties& other) const
    {
        for (int i = 0; i < MaterialCount; i++)
        {
            if (material_revisions[i] != other.material_revisions[i]) return true;
        }

        return clip_id != other.clip_id || color != other.color
            || feature_color_blend_strength != other.feature_color_blend_strength
            || feature_color_blend_mode != other.feature_color_blend_mode
            || overlay_material_enabled != other.overlay_material_enabled
            || overlay_material_opacity != other.overlay_material_opacity
            || apply_feature_color_to_overlay != other.apply_feature_color_to_overlay
            || draw_under_flat_overlays != other.draw_under_flat_overlays;
    }
};

// Draw properties that require rebuilding all primitive data
// and maybe also the mesh data because of materials.
struct PrimitiveDrawProperties
{
    render::LightingSettings lighting;
    size_t material_revisions[MaterialCount] = {};

    bool operator !=(const PrimitiveDrawProperties& other) const
    {
        for (int i = 0; i < MaterialCount; i++)
        {
            if (material_revisions[i] != other.material_revisions[i]) return true;
        }

        return lighting != other.lighting;
    }
};

// Draw properties that require rebuilding only primitive transform data.
// Typically when we change the position or transform of the model.
// We also include phase & speed of animation here because they affect the
// transforms, but don't require any additional work from animations.
struct PrimitiveTransformProperties
{
    lm::dmat4 transform;
    float animation_speed{};
    float animation_phase{};

    bool operator !=(const PrimitiveTransformProperties& other) const
    {
        return transform != other.transform || animation_speed != other.animation_speed
            || animation_phase != other.animation_phase;
    }
};

class BakedModel
{
protected:
    struct BakedNode
    {
        ModelDescriptor::Transform default_transform;
        ModelDescriptor::Transform local_transform;
    };

    struct BakedNodeInstance
    {
        int node_id;
        std::optional<int> parent;
        lm::dmat4 transform;
    };

    static void bake_nodes(
        std::span<const BakedNode> baked_nodes,
        std::span<BakedNodeInstance> baked_nodes_instances,
        const lm::dmat4& root_transform);

    struct Primitive
    {
        my::ResourceHandle vertex_input;
        size_t draw_ubo_offset;
        size_t transform_ubo_offset;
        RenderablePrimitive renderable;
    };

    enum
    {
        Base,
        Overlay,
        _MaterialCount,
    };

    static_assert((size_t)_MaterialCount == (size_t)MaterialCount, "Material count");

    ModelGeometryH _geometry;
    std::optional<ModelMaterialH> _materials[MaterialCount];

    ModelGeometry::Status _geometry_status = ModelGeometry::Status::Loading;
    ModelMaterial::Status _material_status = ModelMaterial::Status::Loading;
    bool _built = false;

    struct PlayingAnimation
    {
        const Animation* animation;
        AnimationPlayer player;
    };

    // Flag set when an animation has updated model transforms
    // during this frame.
    bool _has_animated = false;
    UsedResources<int> _used_animations;
    std::vector<PlayingAnimation> _playing_animations;
    hrz::flat_hash_map<std::string, int> _animation_name_to_id;

    // This is mainly used while we wait for the descriptor to be ready.
    // Otherwise we can directly update the playing animations.
    std::vector<std::string> _queued_animations;

    Observed<MeshDrawProperties> _mesh_prps;
    Observed<PrimitiveDrawProperties> _primitive_draw_prps;
    Observed<PrimitiveTransformProperties> _primitive_transform_prps;

    RenderablePrimitive::MeshRenderData _render_data;

    // These nodes mirror nodes & node instances from the mesh descriptor and
    // store the pre-computed transforms.
    lm::dmat4 _root_transform;
    std::vector<BakedNode> _baked_nodes;
    std::vector<BakedNodeInstance> _baked_nodes_instances;

    // UBO layout
    //
    // The whole mesh will use a single UBO buffer.
    // First we put the mesh data, then we put one primitive draw data for
    // each renderable primitive, and then one primitive transform data for
    // each renderable primitive.
    // This allows us to update transforms independently from other stuff, and
    // thus move and animated meshes "quickly".

    size_t _prim_count = 0;
    std::unique_ptr<std::byte[]> _ubo_data;

    constexpr size_t full_ubo_size(SharedResources* sr) const
    {
        return sr->mesh_ubo_stride
            + _prim_count * (sr->primitive_draw_ubo_stride + sr->primitive_transform_ubo_stride);
    }

    constexpr size_t primitive_draw_ubo_offset(SharedResources* sr, size_t prim_index) const
    {
        return sr->mesh_ubo_stride + prim_index * sr->primitive_draw_ubo_stride;
    }

    constexpr size_t primitive_transform_ubo_offset(SharedResources* sr, size_t prim_index) const
    {
        return sr->mesh_ubo_stride + _prim_count * sr->primitive_draw_ubo_stride
            + prim_index * sr->primitive_transform_ubo_stride;
    }

    std::vector<Primitive> _primitives;

    void build(ModelPrototype*, SharedResources*, Render*);

    void build_primitive(
        ModelPrototype* proto,
        ModelGeometry* geometry,
        const std::array<ModelMaterial*, MaterialCount>& materials,
        size_t primitive_index,
        SharedResources* sr,
        Render* render);

    void build_pre_bake_nodes(ModelPrototype* proto);
    void update_baked_nodes();

    void update_mesh(
        ModelPrototype* proto,
        ModelGeometry*,
        const std::array<ModelMaterial*, MaterialCount>& materials,
        MeshUniformData*,
        SharedResources* sr);

    void update_primitive_draw(
        ModelPrototype* proto,
        ModelGeometry*,
        const std::array<ModelMaterial*, MaterialCount>& materials,
        size_t primitive_index,
        PrimitiveDrawUniformData*,
        SharedResources* sr);

    void update_primitive_transform(
        ModelPrototype* proto,
        ModelGeometry*,
        const std::array<ModelMaterial*, MaterialCount>& materials,
        size_t primitive_index,
        PrimitiveTransformUniformData*,
        SharedResources* sr);

    void update(ModelPrototype* proto, SharedResources* sr, Render* render);

    virtual void inner_draw(
        ModelPrototype*,
        Render*,
        SharedResources* sr,
        AttributionRegistry*,
        void* draw_data);

    virtual void assign_shaders(SharedResources*);

    bool is_animation_loading() const;
    void update_animation_times();

public:
    BakedModel(
        ModelGeometryH geometry,
        std::optional<ModelMaterialH> base_material,
        std::optional<ModelMaterialH> overlay_material);

    virtual ~BakedModel() = default;

    virtual void destroy(ModelPrototype*);

    BakedModelStatus status() const;

    void work(ModelPrototype*);
    RenderRequest work_gpu(ModelPrototype*, SharedResources*, Render*);

    void draw(
        ModelPrototype*,
        const DrawProperties&,
        bool selected,
        uint32_t scene_views,
        SharedResources*,
        Render*,
        AttributionRegistry*,
        void* draw_data);

    // Expensive!
    BSphere<double> compute_bsphere(ModelPrototype*, const lm::dmat4& transform) const;

    void set_animations(ModelPrototype*, std::span<const std::string>);
};

class ImpostorBakingBakedModel : public BakedModel
{
protected:
    ImpostorBakingUniformData _impostor_ubo_data;
    my::ResourceHandle _impostor_ubo = my::ResourceHandle::null();
    bool _impostor_ubo_dirty = false;

    virtual void inner_draw(
        ModelPrototype*,
        Render*,
        SharedResources* sr,
        AttributionRegistry*,
        void* draw_data) override;
    virtual void assign_shaders(SharedResources*) override;

public:
    ImpostorBakingBakedModel(
        ImpostorBakingModelGeometryH geometry,
        std::optional<ModelMaterialH> material);
    virtual void destroy(ModelPrototype*) override;

    void update_impostor_baking_matrices(const lm::mat4& proj, const lm::mat4& view);
};

class InstancedBakedModel : public BakedModel
{
protected:
    virtual void inner_draw(
        ModelPrototype*,
        Render*,
        SharedResources* sr,
        AttributionRegistry*,
        void* draw_data) override;
    virtual void assign_shaders(SharedResources*) override;

public:
    InstancedBakedModel(InstancedModelGeometryH geometry, std::optional<ModelMaterialH> material);
};

class BatchedBakedModel : public BakedModel
{
protected:
    virtual void inner_draw(
        ModelPrototype*,
        Render*,
        SharedResources* sr,
        AttributionRegistry*,
        void* draw_data) override;
    virtual void assign_shaders(SharedResources*) override;

public:
    BatchedBakedModel(
        ModelPrototype*,
        BatchedModelGeometryH geometry,
        std::optional<ModelMaterialH> base_material,
        std::optional<ModelMaterialH> overlay_material);
};

} // namespace hrz::model
