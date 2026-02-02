#pragma once

#include "hrz/core/data_texture.h"
#include "hrz/core/model/common.h"
#include "hrz/core/model/descriptor.h"
#include "hrz/core/model/model.h"
#include "hrz/core/model/renderable.h"
#include "hrz/core/model/ubo_defs.h"
#include "hrz/core/selection_storage.h"

namespace hrz::model
{
struct ModelPrototype;

class ModelGeometry
{
public:
    enum class Status
    {
        Loading,
        Ready,
        Error,
    };

    struct Primitive
    {
        // Addresses ModelGeometry::_streams
        size_t first_stream = 0;
        size_t stream_count = 0;

        lm::dbbox3 bbox;
        int node_instance_id;

        my::DrawBatchInfo batch;
        my::ResourceHandle index_buffer;
        VertexCompressionParamsUniformData position_compression;
        VertexCompressionParamsUniformData normal_compression;
    };

private:
    UsedResources<int> _used_vertex_buffers;
    UsedResources<int> _used_index_buffers;
    UsedResources<int> _used_draco_meshes;

    enum InternalStatus
    {
        Uninitialized,
        Initialized,
        Built,
        Error,
    };

    InternalStatus _status;

    std::vector<Primitive> _primitives;
    std::vector<my::VertexInputStream> _streams;
    bool _has_normals;

    uint32_t _object_id_offset{};
    picking::ObjectReference _object_reference;
    picking::FeatureReference _feature_reference;

    bool ready_to_build() const;

    // Returns true if successful
    bool build_primitive(
        ModelPrototype* proto,
        SharedResources* sr,
        const ModelDescriptor::MeshInstance& desc_mesh,
        const ModelDescriptor::Primitive& desc_prim);

protected:
    virtual void initialize(ModelPrototype*, std::span<const char* const> additional_streams = {});

    virtual void build(ModelPrototype*, SharedResources*, Render*);

    void set_status_to_error() { _status = InternalStatus::Error; }

public:
    ModelGeometry(
        uint32_t object_id_offset,
        const picking::ObjectReference& object_reference,
        const picking::FeatureReference& feature_reference);
    virtual ~ModelGeometry() = default;

    virtual void destroy(ModelPrototype*);

    Status status() const;

    void work(ModelPrototype*);
    RenderRequest work_gpu(ModelPrototype*, SharedResources*, Render*);

    Primitive& get_primitive(size_t i) { return _primitives[i]; }

    std::span<const my::VertexInputStream> get_streams(const Primitive&);
    BSphere<double> compute_bsphere(const lm::dmat4& transform) const;

    void fill_ubo_data(MeshGeometryUniformData*);

    virtual void callback_additional_streams(
        SharedResources*,
        const std::function<void(const AdditionalVertexInputStream&)>& callback);
    virtual void on_add_stream(size_t primitive_index, const my::VertexInputStream& stream);

    virtual int select_shader_collection(size_t primitive_index) const { return 0; }
};

class BatchedModelGeometry : public ModelGeometry
{
    using FeatureIdsTextureResource =
        DataTexture<HRZ_S_B3DM_DATA_TEXTURE_WIDTH, my::TextureFormat::RG32UI, uint64_t>;
    using FeatureColorsTextureResource =
        DataTexture<HRZ_S_B3DM_DATA_TEXTURE_WIDTH, my::TextureFormat::SRGBA8, lm::ubvec4>;

    std::vector<bool> _primitive_has_float_batch_ids;

    FeatureIdsTextureResource _feature_ids_texture;

    selection::SelectionStorageUint32TextureMultiIndex _selection_storage;

    FeatureColorsTextureResource _colors_texture;
    bool _has_transparent_feature_colors = false;

    void initialize(ModelPrototype*, std::span<const char* const> additional_streams = {}) override;
    void build(ModelPrototype*, SharedResources*, Render*) override;

public:
    BatchedModelGeometry(
        ModelPrototype* proto,
        uint32_t object_id_offset,
        const picking::ObjectReference& object_reference,
        const picking::FeatureReference& feature_reference,
        size_t batch_length,
        std::span<const vector_data::FeatureIdHash> feature_id_hashes);

    void destroy(ModelPrototype*) override;

    void callback_additional_streams(
        SharedResources*,
        const std::function<void(const AdditionalVertexInputStream&)>& callback) override;
    void on_add_stream(size_t primitive_index, const my::VertexInputStream& stream) override;

    void set_selection(const hrz::flat_hash_set<vector_data::FeatureIdHash>& selected_objects);
    void set_colors(std::span<const lm::ubvec4>);

    void update_gpu_data(Render*);

    int select_shader_collection(size_t primitive_index) const override
    {
        if (primitive_index < _primitive_has_float_batch_ids.size())
        {
            return _primitive_has_float_batch_ids[primitive_index] ? 1 : 0;
        }
        else
        {
            return 0;
        }
    }

    void patch_render_data(Render*, SharedResources* sr, RenderablePrimitive::MeshRenderData*);
};

} // namespace hrz::model
