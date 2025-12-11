#include "hrz/core/model/geometry.h"

#include "hrz/core/model/cvt_utils.h"
#include "hrz/core/model/prototype.h"

namespace
{

template<typename T, typename Index>
constexpr const T* _get_ptr(const std::vector<T>& v, Index id)
{
    if (id >= 0 && (size_t)id < v.size())
    {
        return &v[(size_t)id];
    }
    else
    {
        return nullptr;
    }
}

hrz::model::ModelGeometry* _get_model_geometry(
    hrz::model::ModelPrototype* proto,
    hrz::model::ModelGeometryH handle)
{
    auto* ptr = proto->geometry_pool.get_object(handle.o);
    assert(ptr);
    return ptr ? ptr->get() : nullptr;
}

inline my::IndexType _get_index_type(my::VertexFormat format)
{
    switch (format)
    {
        case my::VertexFormat::UInt8: return my::IndexType::UByte;
        case my::VertexFormat::UInt16: return my::IndexType::UShort;
        case my::VertexFormat::UInt32: return my::IndexType::UInt;
        default:
            HRZ_LOG_WARNING("Index type {} not supported", (int)format);
            return my::IndexType::UShort;
    }
}

} // namespace

namespace hrz::model
{

ModelGeometry::ModelGeometry(
    uint32_t object_id_offset,
    const picking::ObjectReference& object_reference,
    const picking::FeatureReference& feature_reference) :
    _status(InternalStatus::Uninitialized),
    _has_normals(false),
    _object_id_offset(object_id_offset),
    _object_reference(object_reference),
    _feature_reference(feature_reference)
{
}

void ModelGeometry::initialize(
    ModelPrototype* proto,
    std::span<const char* const> additional_streams)
{
    assert(_status == InternalStatus::Uninitialized);

    proto->iterate_primitives(
        [this, proto, additional_streams](
            const ModelDescriptor::Mesh*, const ModelDescriptor::MeshInstance*,
            const ModelDescriptor::Primitive* primitive)
        {
            bool has_draco_mesh = false;
            if (primitive->draco_buffer_view.has_value())
            {
                proto->start_loading_draco_mesh(
                    primitive->draco_buffer_view.value(), &_used_draco_meshes);
                has_draco_mesh = true;
            }

            if (primitive->indices.has_value() && !has_draco_mesh)
            {
                proto->start_loading_indices(primitive->indices.value(), &_used_index_buffers);
            }

            if (primitive->position.has_value())
            {
                proto->start_loading_attribute(primitive->position.value(), &_used_vertex_buffers);
            }

            if (primitive->normal.has_value())
            {
                proto->start_loading_attribute(primitive->normal.value(), &_used_vertex_buffers);
                _has_normals = true;
            }

            if (primitive->color.has_value())
            {
                proto->start_loading_attribute(primitive->color.value(), &_used_vertex_buffers);
            }

            for (const char* const stream : additional_streams)
            {
                auto it = primitive->extra_attributes.find(stream);
                if (it != primitive->extra_attributes.end())
                {
                    proto->start_loading_attribute(it->second, &_used_vertex_buffers);
                }
            }
        });

    _status = InternalStatus::Initialized;
}

void ModelGeometry::destroy(ModelPrototype* proto)
{
    proto->release_attributes(_used_vertex_buffers);
    proto->release_indices(_used_index_buffers);
    proto->release_draco_meshes(_used_draco_meshes);
}

ModelGeometry::Status ModelGeometry::status() const
{
    switch (_status)
    {
        case InternalStatus::Uninitialized:
        case InternalStatus::Initialized: return Status::Loading;
        case InternalStatus::Built: return Status::Ready;
        case InternalStatus::Error: return Status::Error;
        default: assert(false && "Unhandled case"); return Status::Error;
    }
}

void ModelGeometry::work(ModelPrototype* proto)
{
    if (_status == InternalStatus::Error) return;

    if (_status == InternalStatus::Uninitialized
        && proto->status == ModelPrototype::Status::DescriptorLoaded)
    {
        initialize(proto);
        assert(_status == InternalStatus::Initialized);
    }

    if (_status == InternalStatus::Initialized)
    {
        proto->update_attributes_load_status(_used_vertex_buffers);
        proto->update_indices_load_status(_used_index_buffers);
        proto->update_draco_meshes_load_status(_used_draco_meshes);
    }
}

void ModelGeometry::callback_additional_streams(
    SharedResources*,
    const std::function<void(const AdditionalVertexInputStream&)>& callback)
{
}

void ModelGeometry::on_add_stream(size_t primitive_index, const my::VertexInputStream& stream) {}

bool ModelGeometry::build_primitive(
    ModelPrototype* proto,
    SharedResources* sr,
    const ModelDescriptor::MeshInstance& desc_mesh,
    const ModelDescriptor::Primitive& desc_prim)
{
    const size_t primitive_index = _primitives.size();
    _primitives.emplace_back();

    const GpuDracoMeshResource* draco_mesh = nullptr;
    if (desc_prim.draco_buffer_view.has_value())
    {
        const int draco_id = desc_prim.draco_buffer_view.value();
        draco_mesh = proto->get_draco_mesh(draco_id);
    }

    Primitive prim;
    prim.first_stream = _streams.size();
    prim.stream_count = 0;

    std::optional<lm::dbbox3> bbox;
    std::optional<uint32_t> vertex_count;

    auto add_stream =
        [this, &prim](
            const my::VertexInputStream& actual, const my::VertexInputStream* unused,
            std::optional<std::function<void(const my::VertexInputStream&)>> on_success =
                std::nullopt)
    {
        _streams.push_back(actual);
        prim.stream_count += 1;

        if (on_success.has_value()) (on_success.value())(actual);

        if (unused)
        {
            _streams.push_back(*unused);
            prim.stream_count += 1;
        }
    };

    auto find_and_add_stream =
        [proto, draco_mesh, &bbox, &vertex_count, &add_stream](
            int index, const std::optional<ModelDescriptor::Attribute>& attr_opt,
            const my::VertexInputStream& fallback,
            // There's no std::optional<const ref>...
            const my::VertexInputStream* compressed_fallback,
            VertexCompressionParamsUniformData* compression_ubo_data,
            const std::optional<std::function<void(const my::VertexInputStream&)>>& on_success =
                std::nullopt)
    {
        assert((compressed_fallback != nullptr) == (compression_ubo_data != nullptr));

        if (attr_opt.has_value())
        {
            const auto& attr = attr_opt.value();
            const auto* accessor = _get_ptr(proto->descriptor.accessors, attr.accessor);

            if (accessor && attr.draco_attribute.has_value() && draco_mesh)
            {
                auto it = draco_mesh->attrib_id_to_index.find(attr.draco_attribute.value());
                if (it != draco_mesh->attrib_id_to_index.end())
                {
                    const auto& draco_attr = draco_mesh->mesh.attributes.at((size_t)it->second);

                    my::VertexFormat format =
                        _convert_vertex_format(draco_attr.data_type, draco_attr.component_count);

                    if (draco_attr.normalized)
                    {
                        format = my::to_normalized(format);
                    }

                    my::VertexInputStream stream;
                    stream.index = index;
                    stream.buffer = draco_mesh->vertex_buffer;
                    stream.format = format;
                    stream.offset = draco_attr.offset;
                    stream.stride = draco_attr.stride;
                    stream.rate = my::VertexRate::PerVertex;

                    const DracoCompressionType compression_type =
                        _convert_compression_type(draco_attr.compression);

                    if (compression_ubo_data)
                    {
                        *compression_ubo_data = _to_compression_uniform_data(draco_attr);
                    }
                    else if (compression_type != DracoCompressionType::None)
                    {
                        HRZ_LOG_WARNING("Non standard attributes cannot be Draco-compressed");
                        return false;
                    }

                    if (compression_type == DracoCompressionType::None)
                    {
                        add_stream(stream, compressed_fallback, on_success);
                    }
                    else
                    {
                        stream.index += CompressedStreamOffset;
                        add_stream(stream, &fallback, on_success);
                    }

                    if (index == PositionStreamIndex)
                    {
                        bbox = draco_mesh->mesh.bounding_box;
                        vertex_count = (uint32_t)accessor->count;
                    }

                    return true;
                }
            }
            else if (accessor && accessor->buffer_view.has_value())
            {
                const int buffer_view = accessor->buffer_view.value();
                const auto* buffer_view_res = proto->resources.vertex_buffers.get(buffer_view);

                if (buffer_view_res
                    && proto->resources.vertex_buffers.get_status(buffer_view)
                        == ResourceStatus::Ready)
                {
                    my::VertexInputStream stream;
                    stream.index = index;
                    stream.buffer = buffer_view_res->render_handle;
                    stream.format = accessor->type;
                    stream.offset = accessor->byte_offset;
                    stream.stride = buffer_view_res->stride;
                    stream.rate = my::VertexRate::PerVertex;

                    add_stream(stream, compressed_fallback, on_success);
                    if (compression_ubo_data)
                    {
                        compression_ubo_data->type = DracoCompressionType::None;
                    }

                    if (index == PositionStreamIndex)
                    {
                        vertex_count = (uint32_t)accessor->count;

                        if (accessor->type == my::VertexFormat::Float32_2
                            || accessor->type == my::VertexFormat::Float32_3)
                        {
                            // The spec says: "POSITION accessor must have min and max
                            // properties defined."
                            bbox = lm::dbbox3(accessor->min.xyz, accessor->max.xyz);
                        }
                        else
                        {
                            HRZ_LOG_WARNING(
                                "Position vertex format must be Float32_2 or Float32_3");

                            const double double_max = std::numeric_limits<double>::max();
                            bbox = lm::dbbox3(
                                {double_max, double_max, double_max},
                                {-double_max, -double_max, -double_max});
                        }
                    }

                    return true;
                }
            }
        }

        // Default, when everything else has failed.
        add_stream(fallback, compressed_fallback, on_success);
        if (compression_ubo_data)
        {
            compression_ubo_data->type = DracoCompressionType::None;
        }

        return true;
    };

    bool streams_found = true;

    streams_found &= find_and_add_stream(
        PositionStreamIndex, desc_prim.position, sr->fallback_position_vertex_stream,
        &sr->fallback_compressed_position_vertex_stream, &prim.position_compression);

    streams_found &= find_and_add_stream(
        NormalStreamIndex, desc_prim.normal, sr->fallback_normal_vertex_stream,
        &sr->fallback_compressed_normal_vertex_stream, &prim.normal_compression);

    streams_found &= find_and_add_stream(
        ColorStreamIndex, desc_prim.color, sr->fallback_color_vertex_stream, nullptr, nullptr);

    callback_additional_streams(
        sr,
        [this, &desc_prim, &find_and_add_stream, primitive_index,
         &streams_found](const AdditionalVertexInputStream& stream)
        {
            std::optional<ModelDescriptor::Attribute> attr_opt;

            auto it = desc_prim.extra_attributes.find(stream.name);
            if (it != desc_prim.extra_attributes.end())
            {
                attr_opt = it->second;
            }

            streams_found &= find_and_add_stream(
                stream.index, attr_opt, stream.fallback_stream, nullptr, nullptr,
                [this, primitive_index](const my::VertexInputStream& stream)
                { this->on_add_stream(primitive_index, stream); });
        });

    if (!streams_found)
    {
        return false;
    }

    if (desc_prim.indices.has_value())
    {
        const auto& desc_indices = desc_prim.indices.value();
        const auto* accessor = _get_ptr(proto->descriptor.accessors, desc_indices.accessor);

        if (draco_mesh)
        {
            prim.index_buffer = draco_mesh->index_buffer;
            prim.batch = my::DrawBatchInfo(desc_prim.mode, draco_mesh->mesh.index_count)
                             .indexed(_convert_index_type(draco_mesh->mesh.index_type), 0);
        }
        else if (accessor && accessor->buffer_view.has_value())
        {
            const int buffer_view_id = accessor->buffer_view.value();
            const auto* buffer = proto->resources.index_buffers.get(buffer_view_id);

            if (buffer
                && proto->resources.index_buffers.get_status(buffer_view_id)
                    == ResourceStatus::Ready)
            {
                const my::IndexType index_type = _get_index_type(accessor->type);
                const size_t index_size = my::index_size(index_type);

                if (accessor->byte_offset % index_size != 0)
                {
                    HRZ_LOG_ERROR(
                        "Indices byte offset is not a multiple of the the size of an index. Expect "
                        "stuff to look broken.");
                }

                prim.index_buffer = buffer->render_handle;
                prim.batch =
                    my::DrawBatchInfo(desc_prim.mode, (uint32_t)accessor->count)
                        .indexed(index_type, (uint32_t)(accessor->byte_offset / index_size));
            }
            else
            {
                HRZ_LOG_ERROR("Could not load index buffer");
                return false;
            }
        }
        else
        {
            HRZ_LOG_ERROR("Invalid index buffer");
            return false;
        }
    }
    else if (vertex_count.has_value())
    {
        prim.index_buffer = my::ResourceHandle::null();
        prim.batch = my::DrawBatchInfo(desc_prim.mode, vertex_count.value());
    }
    else
    {
        HRZ_LOG_ERROR("No vertex count found for model {}", proto->descriptor_uri);
        return false;
    }

    prim.node_instance_id = desc_mesh.node_instance_id;
    prim.bbox = bbox.value_or(lm::dbbox3());

    _primitives.back() = std::move(prim);

    return true;
}

void ModelGeometry::build(ModelPrototype* proto, SharedResources* sr, Render*)
{
    assert(_status == InternalStatus::Initialized);

    bool all_primitives_built = true;

    proto->iterate_primitives(
        [this, proto, sr, &all_primitives_built](
            const ModelDescriptor::Mesh*, const ModelDescriptor::MeshInstance* mesh_instance,
            const ModelDescriptor::Primitive* primitive)
        {
            all_primitives_built =
                build_primitive(proto, sr, *mesh_instance, *primitive) && all_primitives_built;
        });

    if (all_primitives_built)
    {
        _status = InternalStatus::Built;
    }
    else
    {
        _status = InternalStatus::Error;
    }
}

RenderRequest ModelGeometry::work_gpu(ModelPrototype* proto, SharedResources* sr, Render* render)
{
    if (_status != InternalStatus::Initialized) return {};

    if (ready_to_build())
    {
        build(proto, sr, render);

        if (_status == InternalStatus::Built)
        {
            return RenderRequest::visual();
        }
    }

    return {};
}

bool ModelGeometry::ready_to_build() const
{
    return _status == InternalStatus::Initialized && _used_draco_meshes.all_ready()
        && _used_index_buffers.all_ready() && _used_vertex_buffers.all_ready();
}

std::span<const my::VertexInputStream> ModelGeometry::get_streams(const Primitive& prim)
{
    return {_streams.data() + prim.first_stream, prim.stream_count};
}

void ModelGeometry::fill_ubo_data(MeshGeometryUniformData* data)
{
    data->object_reference = _object_reference.to_uvec2();
    data->feature_reference = _feature_reference.to_uvec3();
    data->object_id_offset = _object_id_offset;

    // "When normals are not specified, client implementations MUST calculate flat normals"
    data->flat_shaded = !_has_normals;
}

BatchedModelGeometry::BatchedModelGeometry(
    ModelPrototype* proto,
    uint32_t object_id_offset,
    const picking::ObjectReference& object_reference,
    const picking::FeatureReference& feature_reference,
    size_t batch_length,
    std::span<const vector_data::FeatureIdHash> feature_id_hashes) :
    ModelGeometry(object_id_offset, object_reference, feature_reference),
    _feature_ids_texture(
        proto->resource_owner,
        {{"model URI"_ss, proto->descriptor_uri}, {"contents"_ss, "feature ids texture"_ss}}),
    _colors_texture(
        proto->resource_owner,
        {{"model URI"_ss, proto->descriptor_uri}, {"contents"_ss, "feature colors"_ss}})
{
    _feature_ids_texture.set(feature_id_hashes);

    _selection_storage = selection::SelectionStorageUint32TextureMultiIndex(
        feature_id_hashes.size(), proto->resource_owner, {{"model URI"_ss, proto->descriptor_uri}});
    if (feature_id_hashes.size() == batch_length)
    {
        for (size_t batch_id = 0; batch_id < feature_id_hashes.size(); ++batch_id)
        {
            _selection_storage.register_indirection(
                feature_id_hashes[batch_id], (uint32_t)batch_id);
        }
    }

    // Minimum 1 color (so that it's not invisible when we have batch length = 0)
    const size_t colors_count = std::max((size_t)1, batch_length);
    const std::span<lm::ubvec4> colors = _colors_texture.resize_and_get_data(colors_count);
    std::fill_n(colors.data(), colors.size(), lm::ubvec4(0xffU)); // All white
}

void BatchedModelGeometry::destroy(ModelPrototype* proto)
{
    _feature_ids_texture.destroy(proto->to_destroy);
    _colors_texture.destroy(proto->to_destroy);
    _selection_storage.free_gpu_resources(proto->to_destroy);
    ModelGeometry::destroy(proto);
}

void BatchedModelGeometry::initialize(
    ModelPrototype* proto,
    std::span<const char* const> additional_streams)
{
    assert(additional_streams.size() == 0);
    static constexpr const char* names[] = {"_BATCHID"};
    ModelGeometry::initialize(proto, names);
}

void BatchedModelGeometry::callback_additional_streams(
    SharedResources* sr,
    const std::function<void(const AdditionalVertexInputStream&)>& callback)
{
    callback({"_BATCHID", B3dm_BatchIdStreamIndex, sr->fallback_batch_id_vertex_stream});
}

void BatchedModelGeometry::on_add_stream(
    size_t primitive_index,
    const my::VertexInputStream& stream)
{
    bool has_float_batch_ids = false;

    if (stream.index == B3dm_BatchIdStreamIndex)
    {
        if (stream.format == my::VertexFormat::Float16
            || stream.format == my::VertexFormat::Float32)
        {
            has_float_batch_ids = true;
        }
    }

    if (_primitive_has_float_batch_ids.size() <= primitive_index)
    {
        _primitive_has_float_batch_ids.resize(primitive_index + 1);
    }
    _primitive_has_float_batch_ids[primitive_index] = has_float_batch_ids;
}

void BatchedModelGeometry::build(ModelPrototype* proto, SharedResources* sr, Render* render)
{
    assert(!_feature_ids_texture.get_resource());
    _feature_ids_texture.update(render);

    assert(!_colors_texture.get_resource());
    _colors_texture.update(render);

    if (_feature_ids_texture.status() == DataTextureStatus::Error
        || _colors_texture.status() == DataTextureStatus::Error)
    {
        set_status_to_error();
    }
    else
    {
        ModelGeometry::build(proto, sr, render);
    }
}

void BatchedModelGeometry::set_selection(
    const hrz::flat_hash_set<vector_data::FeatureIdHash>& selected_objects)
{
    _selection_storage.update_selection(selected_objects);
}

void BatchedModelGeometry::set_colors(std::span<const lm::ubvec4> colors)
{
    _colors_texture.set(colors);

    _has_transparent_feature_colors = false;
    for (const auto& color : colors)
    {
        if (color.a > 0 && color.a < 255)
        {
            _has_transparent_feature_colors = true;
            break;
        }
    }
}

void BatchedModelGeometry::update_gpu_data(Render* render)
{
    _selection_storage.work_gpu(render);
    _colors_texture.update(render);
}

void BatchedModelGeometry::patch_render_data(
    Render* render,
    SharedResources* sr,
    RenderablePrimitive::MeshRenderData* render_data)
{
    my::TextureBinding texture_bindings[] = {
        {B3dm_FeatureSelectionSampler, _selection_storage.get_texture(render),
         sr->fallback_data_sampler},
        {B3dm_FeatureColorSampler, _colors_texture.get_resource(), sr->fallback_data_sampler},
        {B3dm_FeatureIdsSampler, _feature_ids_texture.get_resource(), sr->fallback_data_sampler},
    };

    render_data->texture_bindings = {
        render->rd->as_queue().write_n(texture_bindings, HRZ_ARRAY_COUNT(texture_bindings)),
        HRZ_ARRAY_COUNT(texture_bindings)};

    render_data->is_transparent |= _has_transparent_feature_colors;
    render_data->render_selection |= _selection_storage.has_any_selected();
}

SingleModelGeometryH create_single_model_geometry(
    ModelPrototype* proto,
    const picking::ObjectReference& obj_ref,
    const picking::FeatureReference& feature_ref)
{
    std::unique_ptr<ModelGeometry> geometry(new ModelGeometry(0, obj_ref, feature_ref));
    return {proto->geometry_pool.alloc(std::move(geometry))};
}

ImpostorBakingModelGeometryH create_impostor_baking_model_geometry(ModelPrototype* proto)
{
    std::unique_ptr<ModelGeometry> geometry(new ModelGeometry(0, {}, {}));
    return {proto->geometry_pool.alloc(std::move(geometry))};
}

InstancedModelGeometryH create_instanced_model_geometry(ModelPrototype* proto)
{
    std::unique_ptr<ModelGeometry> geometry(new ModelGeometry(0, {}, {}));
    return {proto->geometry_pool.alloc(std::move(geometry))};
}

void set_batched_selection(
    ModelPrototype* proto,
    BatchedModelGeometryH handle,
    const hrz::flat_hash_set<vector_data::FeatureIdHash>& selected_objects)
{
    auto* model = (BatchedModelGeometry*)_get_model_geometry(proto, handle);
    if (model)
    {
        model->set_selection(selected_objects);
    }
}

void set_batched_colors(
    ModelPrototype* proto,
    BatchedModelGeometryH handle,
    std::span<const lm::ubvec4> colors)
{
    auto* model = (BatchedModelGeometry*)_get_model_geometry(proto, handle);
    if (model)
    {
        model->set_colors(colors);
    }
}

BatchedModelGeometryH create_batched_model_geometry(
    ModelPrototype* proto,
    const picking::ObjectReference& obj_ref,
    const picking::FeatureReference& feature_ref,
    uint32_t batch_id_offset,
    size_t batch_length,
    std::span<const vector_data::FeatureIdHash> feature_id_hashes)
{
    std::unique_ptr<ModelGeometry> geometry(new BatchedModelGeometry(
        proto, batch_id_offset, obj_ref, feature_ref, batch_length, feature_id_hashes));
    return {proto->geometry_pool.alloc(std::move(geometry))};
}

void destroy(ModelPrototype* proto, ModelGeometryH handle)
{
    auto* model = _get_model_geometry(proto, handle);
    if (model)
    {
        model->destroy(proto);
        proto->geometry_pool.release(handle.o);
    }
}

} // namespace hrz::model
