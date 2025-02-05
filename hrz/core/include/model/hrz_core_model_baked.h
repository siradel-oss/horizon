#pragma once

#include "model/hrz_core_model_geometry.h"
#include "model/hrz_core_model_instance_group.h"
#include "model/hrz_core_model_material.h"

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
    float overlay_material_opacity = 1.0f;
    bool draw_under_flat_overlays = false;

    bool operator!=(const MeshDrawProperties& other) const
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
// and maybe also the mesh data
struct PrimitiveDrawProperties
{
    render::LightingSettings lighting;

    // @Todo One day we might want to separate model and primitive transforms
    // so that we can update a model position without updating the primitives.
    // But for now it's not really an issue.
    lm::dmat4 transform;
    size_t material_revisions[MaterialCount] = {};

    bool operator!=(const PrimitiveDrawProperties& other) const
    {
        for (int i = 0; i < MaterialCount; i++)
        {
            if (material_revisions[i] != other.material_revisions[i]) return true;
        }

        return transform != other.transform || lighting != other.lighting;
    }
};

class BakedModel
{
protected:
    struct Primitive
    {
        my::ResourceHandle vertex_input;
        size_t ubo_offset;
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

    Observed<MeshDrawProperties> _mesh_prps;
    Observed<PrimitiveDrawProperties> _primitive_prps;

    RenderablePrimitive::MeshRenderData _render_data;
    size_t _ubo_size = 0;

    std::vector<Primitive> _primitives;

    void build(ModelPrototype*, SharedResources*, Render*);

    void build_primitive(
        ModelPrototype* proto,
        ModelGeometry* geometry,
        const std::array<ModelMaterial*, MaterialCount>& materials,
        size_t primitive_index,
        SharedResources* sr,
        Render* render);

    void update_mesh(
        ModelPrototype* proto,
        ModelGeometry*,
        const std::array<ModelMaterial*, MaterialCount>& materials,
        MeshUniformData*,
        SharedResources* sr);

    void update_primitive(
        ModelPrototype* proto,
        ModelGeometry*,
        const std::array<ModelMaterial*, MaterialCount>& materials,
        size_t primitive_index,
        PrimitiveUniformData*,
        SharedResources* sr);

    void update(ModelPrototype* proto, SharedResources* sr, Render* render);

    virtual void inner_draw(
        ModelPrototype*,
        Render*,
        SharedResources* sr,
        AttributionRegistry*,
        void* draw_data);
    virtual void assign_shaders(SharedResources*);

public:
    BakedModel(
        ModelGeometryH geometry,
        std::optional<ModelMaterialH> base_material,
        std::optional<ModelMaterialH> overlay_material);

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
