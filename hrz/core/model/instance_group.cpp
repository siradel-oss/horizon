// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/model/instance_group.h"

#include "hrz/common/geo.h"
#include "hrz/core/model/prototype.h"

namespace hrz::model
{

InstanceGroup::InstanceGroup(
    uint32_t object_id_offset,
    const picking::ObjectReference& obj_ref,
    const picking::FeatureReference& feature_ref,
    const monitoring::ResourceOwner& resource_owner,
    std::string_view model_uri) :
    _positions_texture(
        resource_owner,
        {{"contents", "instance positions"}, {"model URI", model_uri}}),
    _compressed_positions_texture(
        resource_owner,
        {{"contents", "instance compressed positions"}, {"model URI", model_uri}}),
    _normals_texture(resource_owner, {{"contents", "instance normals"}, {"model URI", model_uri}}),
    _compressed_normals_texture(
        resource_owner,
        {{"contents", "instance compressed normals"}, {"model URI", model_uri}}),
    _scales_texture(resource_owner, {{"contents", "instance scales"}, {"model URI", model_uri}}),
    _colors_texture(resource_owner, {{"contents", "instance colors"}, {"model URI", model_uri}}),
    _picking_ids_texture(
        resource_owner,
        {{"contents", "instance picking IDs"}, {"model URI", model_uri}}),
    _feature_ids_texture(
        resource_owner,
        {{"contents", "instance feature IDs"}, {"model URI", model_uri}})
{
    _ubo_data.object_id_offset = object_id_offset;
    _ubo_data.object_reference = obj_ref.to_uvec2();
    _ubo_data.feature_reference = feature_ref.to_uvec3();
}

void InstanceGroup::destroy(ModelPrototype* proto)
{
    if (_ubo)
    {
        proto->to_destroy.push_back(_ubo);
    }

    _positions_texture.destroy(proto->to_destroy);
    _compressed_positions_texture.destroy(proto->to_destroy);
    _normals_texture.destroy(proto->to_destroy);
    _compressed_normals_texture.destroy(proto->to_destroy);
    _scales_texture.destroy(proto->to_destroy);
    _colors_texture.destroy(proto->to_destroy);
    _picking_ids_texture.destroy(proto->to_destroy);
    _feature_ids_texture.destroy(proto->to_destroy);

    _selection_storage.free_gpu_resources(proto->to_destroy);
}

InstanceGroupStatus InstanceGroup::status() const
{
    if (_needs_to_upload_data) return InstanceGroupStatus::Loading;

    DataTextureStatus texture_statuses[8];
    texture_statuses[0] = _positions_texture.status();
    texture_statuses[1] = _normals_texture.status();
    texture_statuses[2] = _scales_texture.status();
    texture_statuses[3] = _colors_texture.status();
    texture_statuses[4] = _picking_ids_texture.status();
    texture_statuses[5] = _feature_ids_texture.status();
    texture_statuses[6] = _compressed_positions_texture.status();
    texture_statuses[7] = _compressed_normals_texture.status();

    for (size_t i = 0; i < 8; ++i)
    {
        auto status = texture_statuses[i];
        if (status == DataTextureStatus::Error) return InstanceGroupStatus::Error;
        if (status == DataTextureStatus::Uninitialized || status == DataTextureStatus::Stale)
        {
            return InstanceGroupStatus::Loading;
        }
    }

    return InstanceGroupStatus::Ready;
}

void InstanceGroup::work_gpu(ModelPrototype* proto, Render* render)
{
    _selection_storage.work_gpu(render);

    if (_needs_to_upload_data)
    {
        if (_ubo.is_null())
        {
            my::BufferResource ubo_res(my::BufferResource::Uniform);
            ubo_res.size = sizeof(InstanceGroupUniformData);
            ubo_res.usage = my::UsageHint::Updatable;
            ubo_res.data = &_ubo_data;
            _ubo = render->rc->alloc(
                &ubo_res, proto->resource_owner, {{"model URI"_ss, proto->descriptor_uri}});
        }
        else
        {
            render->my->update_buffer(_ubo, 0, sizeof(InstanceGroupUniformData), &_ubo_data);
        }

        _positions_texture.update(render);
        _compressed_positions_texture.update(render);
        _normals_texture.update(render);
        _compressed_normals_texture.update(render);
        _scales_texture.update(render);
        _colors_texture.update(render);
        _picking_ids_texture.update(render);
        _feature_ids_texture.update(render);

        _needs_to_upload_data = false;
    }
}

// We compute the bounding sphere of instanced models in two parts:
//    - The points bsphere, computed here.
//      We just compute the bsphere of the points, and the maximum
//      scale of all points.
//    - The bsphere of each primitive.
// When we render a tile, we combine those informations to get the bounding
// sphere of the primitives instanced on the points.
// To do so we expand the first bounding sphere by the primitive radius
// multiplied by the maximum scale.
// Note that we force the center of the primitive bbox to be 0 so that
// this works correctly (because we don't take into account the rotation of
// each instance, so otherwise we would offset the points bsphere by the
// center of the primitive bsphere, but since at that point we want the
// computation to be fast, we can't transform each bsphere individually).
// Note that all of this can only work well because the models are supposed
// to be small compared to the tiles they are instanced on. If it isn't the
// case, the resulting bsphere will be way too large.
BSphere<double> InstanceGroup::compute_instanced_primitive_bsphere(
    const BSphere<double>& prim) const
{
    double dilation_radius = (prim.radius + lm::length(prim.center)) * _max_scale;

    // We can grow the instanced points bounding sphere using this one (a sort
    // of dilation) to approximate the bounding sphere of all the instanced
    // primitives.
    return BSphere<double>{_bsphere.center, _bsphere.radius + dilation_radius};
}

void InstanceGroup::set_data(ModelPrototype* proto, const InstanceGroupData& group_data)
{
    assert(!group_data.positions.empty() || !group_data.compressed_positions.empty());

    lm::dvec3 group_position = lm::dvec3(
        group_data.transform.col[3].x, group_data.transform.col[3].y,
        group_data.transform.col[3].z);
    hrz::split_double(group_position.x, _ubo_data.origin_low.x, _ubo_data.origin_high.x);
    hrz::split_double(group_position.y, _ubo_data.origin_low.y, _ubo_data.origin_high.y);
    hrz::split_double(group_position.z, _ubo_data.origin_low.z, _ubo_data.origin_high.z);

    _transform = group_data.transform;

    const bool has_feature_ids_per_object = !group_data.feature_id_per_object.empty();
    const bool has_feature_ids_per_instance = !group_data.feature_id_per_instance.empty();

    bool has_normals = !group_data.normals.empty() || !group_data.compressed_normals.empty();
    bool use_compressed_normals =
        !group_data.compressed_normals.empty() && group_data.normals.empty();
    bool use_compressed_positions =
        !group_data.compressed_positions.empty() && group_data.positions.empty();

    _instance_count = use_compressed_positions ? group_data.compressed_positions.size()
                                               : group_data.positions.size();

    bool has_unique_scale =
        _instance_count != group_data.scales.size() && group_data.scales.size() == 1;
    bool has_unique_color =
        _instance_count != group_data.colors.size() && group_data.colors.size() == 1;

    _ubo_data.linear_transform = lm::mat3(group_data.transform);
    _ubo_data.position_compression = group_data.position_compression;
    _ubo_data.normal_compression = group_data.normal_compression;

    assert(group_data.normals.empty() || group_data.normals.size() == 2 * _instance_count);
    assert(
        group_data.compressed_normals.empty()
        || group_data.compressed_normals.size() == _instance_count);
    assert(has_unique_scale || group_data.scales.size() == _instance_count);
    assert(has_unique_color || group_data.colors.size() == _instance_count);
    assert(group_data.object_ids.size() == _instance_count);

    // @Todo Don't override selection at every update!
    _selection_storage = selection::SelectionStorageUint32TextureMultiIndex(
        _instance_count, proto->resource_owner, {{"model URI"_ss, proto->descriptor_uri}});

    // @Todo Handle group resize
    std::span<lm::vec3> instance_positions =
        _positions_texture.resize_and_get_data(use_compressed_positions ? 0 : _instance_count);
    std::span<lm::usvec3> instance_compressed_positions =
        _compressed_positions_texture.resize_and_get_data(
            use_compressed_positions ? _instance_count : 0);
    std::span<lm::vec3> instance_normals =
        _normals_texture.resize_and_get_data(use_compressed_normals ? 0 : 2 * _instance_count);
    std::span<lm::usvec4> instance_compressed_normals =
        _compressed_normals_texture.resize_and_get_data(
            use_compressed_normals ? _instance_count : 0);
    std::span<lm::vec3> instance_scales = _scales_texture.resize_and_get_data(
        std::min(_instance_count, (uint32_t)group_data.scales.size()));
    std::span<lm::ubvec4> instance_colors = _colors_texture.resize_and_get_data(
        std::min(_instance_count, (uint32_t)group_data.colors.size()));
    std::span<uint32_t> picking_ids = _picking_ids_texture.resize_and_get_data(_instance_count);
    std::span<uint64_t> feature_ids = _feature_ids_texture.resize_and_get_data(_instance_count);

    // @Todo Reset selection indirection on resize maybe

    _has_transparent_color = false;
    _max_scale = 0;

    std::vector<lm::dvec3> points;
    points.reserve(_instance_count);

    lm::dmat3 group_linear_xform = lm::dmat3(
        lm::dvec3(group_data.transform.x.xyz), lm::dvec3(group_data.transform.y.xyz),
        lm::dvec3(group_data.transform.z.xyz));

    auto fetch_instance_position = [&](uint32_t instance) -> lm::dvec3
    {
        if (use_compressed_positions)
        {
            return lm::dvec3(group_data.compressed_positions[instance])
                * group_data.position_compression.quantization_scale
                + group_data.position_compression.quantization_mins;
        }
        else
        {
            return lm::dvec3(group_data.positions[instance]);
        }
    };

    for (uint32_t instance = 0; instance < _instance_count; ++instance)
    {
        lm::dvec3 local_position = fetch_instance_position(instance);
        lm::dvec3 global_position = group_data.transform.w.xyz + local_position;

        lm::dvec4 x, y;
        if (has_normals)
        {
            if (use_compressed_normals)
            {
                x = lm::dvec4(1, 0, 0, 0);
                y = lm::dvec4(0, 1, 0, 0);
            }
            else
            {
                x.xyz = lm::normalize(group_linear_xform * group_data.normals[2 * instance + 0]);
                y.xyz = lm::normalize(group_linear_xform * group_data.normals[2 * instance + 1]);
            }
        }
        else if (group_data.use_enu_orientation)
        {
            lm::dmat4 enu_frame =
                hrz::ecef_to_enu_rotation_matrix_for_geo(hrz::ecef_to_geo2(global_position));
            x = lm::dvec4(enu_frame.x.x, enu_frame.y.x, enu_frame.z.x, 0);
            y = lm::dvec4(enu_frame.x.y, enu_frame.y.y, enu_frame.z.y, 0);
        }
        else
        {
            x = lm::dvec4(1, 0, 0, 0);
            y = lm::dvec4(0, 1, 0, 0);
        }
        lm::dvec4 z = lm::dvec4(lm::normalize(lm::cross(x.xyz, y.xyz)), 0);

        lm::dvec3 scale = has_unique_scale ? lm::dvec3(group_data.scales[0])
                                           : lm::dvec3(group_data.scales[instance]);

        lm::dmat4 transform = lm::dmat4(
            lm::dvec4(x * scale.x), lm::dvec4(y * scale.y), lm::dvec4(z * scale.z),
            lm::dvec4(local_position, 1));
        _max_scale = std::max(_max_scale, (double)std::abs(lm::determinant(lm::mat3(transform))));

        if (use_compressed_positions)
        {
            instance_compressed_positions[instance] =
                lm::usvec3(group_data.compressed_positions[instance]);
        }
        else
        {
            instance_positions[instance] = group_data.positions[instance];
        }

        if (use_compressed_normals)
        {
            instance_compressed_normals[instance] = group_data.compressed_normals[instance];
        }
        else
        {
            instance_normals[2 * instance + 0] = lm::vec3(x.xyz);
            instance_normals[2 * instance + 1] = lm::vec3(y.xyz);
        }

        uint32_t scale_index = has_unique_scale ? 0 : instance;
        instance_scales[scale_index] = lm::vec3(group_data.scales[scale_index]);

        uint32_t color_index = has_unique_color ? 0 : instance;
        instance_colors[color_index] = group_data.colors[color_index];
        if (instance_colors[color_index].a != 0xff && instance_colors[color_index].a != 0)
        {
            _has_transparent_color = true;
        }

        const uint32_t object_id = group_data.object_ids[instance];
        picking_ids[instance] = object_id;

        if (has_feature_ids_per_instance)
        {
            feature_ids[instance] = group_data.feature_id_per_instance[instance];
        }
        else if (has_feature_ids_per_object)
        {
            feature_ids[instance] = group_data.feature_id_per_object[object_id];
        }
        else
        {
            feature_ids[instance] = object_id;
        }

        _selection_storage.register_indirection(feature_ids[instance], instance);

        points.push_back(global_position);
    }

    _bsphere = compute_bounding_sphere<double>(points);
    _needs_to_upload_data = true;
}

void InstanceGroup::set_colors(ModelPrototype*, std::span<const lm::ubvec4> instance_colors)
{
    _colors_texture.set(instance_colors);

    for (const auto& color : instance_colors)
    {
        if (color.a != 0xff && color.a != 0)
        {
            _has_transparent_color = true;
            break;
        }
    }

    _needs_to_upload_data = true;
}

void InstanceGroup::set_selection(const hrz::flat_hash_set<uint64_t>& selected_objects)
{
    _selection_storage.update_selection(selected_objects);
}

std::span<const my::UboBinding> InstanceGroup::write_ubo_bindings(Render* render, SharedResources*)
{
    my::UboBinding binding = {UboGroupParams, _ubo, 0, sizeof(InstanceGroupUniformData)};
    return {render->rd->as_queue().write(binding), 1};
}

std::span<const my::TextureBinding> InstanceGroup::write_texture_bindings(
    Render* render,
    SharedResources* sr)
{
    const my::TextureBinding bindings[] = {
        {Instanced_SelectionSampler, _selection_storage.get_texture(render),
         sr->fallback_data_sampler},
        {Instanced_PositionSampler, _positions_texture.get_resource(), sr->fallback_data_sampler},
        {Instanced_CompressedPositionSampler, _compressed_positions_texture.get_resource(),
         sr->fallback_data_sampler},
        {Instanced_NormalSampler, _normals_texture.get_resource(), sr->fallback_data_sampler},
        {Instanced_CompressedNormalSampler, _compressed_normals_texture.get_resource(),
         sr->fallback_data_sampler},
        {Instanced_ScaleSampler, _scales_texture.get_resource(), sr->fallback_data_sampler},
        {Instanced_ColorSampler, _colors_texture.get_resource(), sr->fallback_data_sampler},
        {Instanced_ObjectIdSampler, _picking_ids_texture.get_resource(), sr->fallback_data_sampler},
        {Instanced_FeatureIdSampler, _feature_ids_texture.get_resource(),
         sr->fallback_data_sampler},
    };

    return render->rd->as_queue().write_n(bindings);
}

void InstanceGroup::patch_primitive(RenderablePrimitive* prim) const
{
    prim->bsphere = compute_instanced_primitive_bsphere(prim->bsphere);
    prim->primitive_data.is_transparent |= _has_transparent_color;
    prim->primitive_data.batch = std::move(prim->primitive_data.batch).instanced(_instance_count);
}

InstanceGroupH create_instance_group(
    ModelPrototype* proto,
    const picking::ObjectReference& obj_ref,
    const picking::FeatureReference& feature_ref,
    uint32_t object_id_offset,
    const InstanceGroupData& data)
{
    uint64_t handle = proto->instance_group_pool.alloc(
        object_id_offset, obj_ref, feature_ref, proto->resource_owner, proto->descriptor_uri);
    auto* group = proto->instance_group_pool.get_object(handle);
    group->set_data(proto, data);
    return InstanceGroupH{handle};
}

void destroy(ModelPrototype* proto, InstanceGroupH handle)
{
    InstanceGroup* group = proto->instance_group_pool.get_object(handle.o);
    if (group)
    {
        group->destroy(proto);
        proto->instance_group_pool.release(handle.o);
    }
}

void set_instance_group_colors(
    ModelPrototype* proto,
    InstanceGroupH handle,
    std::span<const lm::ubvec4> instance_colors)
{
    InstanceGroup* group = proto->instance_group_pool.get_object(handle.o);
    if (group)
    {
        group->set_colors(proto, instance_colors);
    }
}

void set_instance_group_selection(
    ModelPrototype* proto,
    InstanceGroupH handle,
    const hrz::flat_hash_set<uint64_t>& selected_objects)
{
    InstanceGroup* group = proto->instance_group_pool.get_object(handle.o);
    if (group)
    {
        group->set_selection(selected_objects);
    }
}

InstanceGroupStatus get_instance_group_status(ModelPrototype* proto, InstanceGroupH handle)
{
    InstanceGroup* group = proto->instance_group_pool.get_object(handle.o);
    if (group)
    {
        return group->status();
    }
    else
    {
        return InstanceGroupStatus::Error;
    }
}

} // namespace hrz::model
