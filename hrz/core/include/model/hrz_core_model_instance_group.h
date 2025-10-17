#pragma once

#include "hrz_core_data_texture.h"
#include "hrz_core_selection_storage.h"
#include "model/hrz_core_model.h"
#include "model/hrz_core_model_common.h"
#include "model/hrz_core_model_gpu_resources.h"
#include "model/hrz_core_model_renderable.h"

namespace hrz::model
{
struct ModelPrototype;

class InstanceGroup
{
    static const uint32_t Width = HRZ_S_INSTANCE_GROUP_DATA_TEXTURE_WIDTH;

    using InstancePositionTextureResource = DataTexture<Width, my::TextureFormat::RGB32F, lm::vec3>;
    using InstanceCompressedPositionTextureResource =
        DataTexture<Width, my::TextureFormat::RGB16UI, lm::usvec3>;
    using InstanceNormalTextureResource = DataTexture<Width, my::TextureFormat::RGB32F, lm::vec3>;
    using InstanceCompressedNormalTextureResource =
        DataTexture<Width, my::TextureFormat::RGBA16UI, lm::usvec4>;
    using InstanceScaleTextureResource = DataTexture<Width, my::TextureFormat::RGB32F, lm::vec3>;
    using InstanceColorTextureResource = DataTexture<Width, my::TextureFormat::RGBA8, lm::ubvec4>;
    using InstancePickingIdsResource = DataTexture<Width, my::TextureFormat::R32UI, uint32_t>;
    using InstanceFeatureIdsResource = DataTexture<Width, my::TextureFormat::RG32UI, uint64_t>;
    static_assert(
        sizeof(vector_data::FeatureIdHash) == sizeof(uint64_t),
        "Unsupported feature ID hash size");

    lm::dmat4 _transform;
    uint32_t _instance_count = 0;

    InstanceGroupUniformData _ubo_data;
    my::ResourceHandle _ubo = my::ResourceHandle::null();

    InstancePositionTextureResource _positions_texture;
    InstanceCompressedPositionTextureResource _compressed_positions_texture;
    InstanceNormalTextureResource _normals_texture;
    InstanceCompressedNormalTextureResource _compressed_normals_texture;
    InstanceScaleTextureResource _scales_texture;
    InstanceColorTextureResource _colors_texture;
    InstancePickingIdsResource _picking_ids_texture;
    InstanceFeatureIdsResource _feature_ids_texture;

    selection::SelectionStorageUint32TextureMultiIndex _selection_storage;

    bool _has_transparent_color = false;
    bool _needs_to_upload_data = false;

    BSphere<double> _bsphere;
    double _max_scale = 0;

    BSphere<double> compute_instanced_primitive_bsphere(const BSphere<double>&) const;

public:
    InstanceGroup(
        uint32_t object_id_offset,
        const picking::ObjectReference& obj_ref,
        const picking::FeatureReference& feature_ref,
        const monitoring::ResourceOwner& resource_owner,
        std::string_view model_uri);
    void destroy(ModelPrototype*);

    InstanceGroupStatus status() const;

    void work_gpu(ModelPrototype*, Render*);
    void set_data(ModelPrototype*, const InstanceGroupData&);
    void set_colors(ModelPrototype*, std::span<const lm::ubvec4> instance_colors);
    void set_selection(const hrz::flat_hash_set<uint64_t>& selected_objects);

    constexpr bool has_selected_features() const { return _selection_storage.has_any_selected(); }

    void patch_primitive(RenderablePrimitive* prim) const;

    std::span<const my::UboBinding> write_ubo_bindings(Render*, SharedResources*);
    std::span<const my::TextureBinding> write_texture_bindings(Render*, SharedResources*);
};

} // namespace hrz::model
