// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/profiling.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/three_d_tiles_jobs_params.h"
#include "hrz/core/loading_priorities.h"
#include "hrz/core/model/descriptor.h"
#include "hrz/core/model/model.h"
#include "hrz/core/three_d_tiles/feature_table_parser.h"
#include "hrz/core/three_d_tiles/model_cache.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/string_utils.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

namespace hrz::three_d_tiles
{

bool ThreeDTile::I3dmContent::decode_i3dm_feature_table(
    ThreeDTile::Subtile* subtile,
    std::span<const std::byte> feature_table_json_data,
    const hrz::blobs::BlobHandle& feature_table_bin_blob)
{
    HRZ_SCOPED_SAMPLE("decode i3dm feature table");

    if (feature_table_json_data.empty())
    {
        return true;
    }

    auto& content = std::get<ThreeDTile::I3dmContent>(subtile->content);

    auto parser_opt = hrz::three_d_tiles::FeatureTableParser::make(
        feature_table_json_data, feature_table_bin_blob);
    if (!parser_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not create feature table parser");
        return false;
    }

    const auto parser = std::move(parser_opt.value());

    if (const auto instances_length = parser.get_global_uint32("INSTANCES_LENGTH");
        instances_length.has_value())
    {
        content._instances_length = instances_length.value();
    }
    else
    {
        HRZ_LOG_ERROR("Could not get INSTANCES_LENGTH from feature table");
        return false;
    }

    if (content._instances_length == 0)
    {
        return true;
    }

    const auto rtc_center = parser.get_global_dvec3("RTC_CENTER").value_or(lm::dvec3{});
    subtile->rtc_transform = lm::translation(rtc_center);

    content._use_east_north_up_orientation =
        parser.get_global_bool("EAST_NORTH_UP").value_or(false);

    if (parser.has_semantic("POSITION"))
    {
        if (!parser.copy_data("POSITION", content._instances_length, content._position_data))
        {
            HRZ_LOG_ERROR("Could not get POSITION data from feature table");
            return false;
        }

        if (rtc_center == lm::dvec3{})
        {
            // Some datasets use large numbers for positions instead of specifying a value for
            // RTC_CENTER. This does not allow a centimetric precision for where instances are
            // placed, but does not always prevent having stable (i.e. non-jittering) instances
            // in the scene, if the tiles are not too large.
            // The centre-point of all positions is used a substitute for RTC_CENTER, and is
            // substracted from the positions. This way if the tile is geographically
            // not too large, instances are stable since large values are not manipulated in
            // single precision on the GPU.
            auto position_bbox = lm::dbbox3::invalid();
            for (const auto& position : content._position_data)
            {
                position_bbox = lm::expand(position_bbox, lm::dvec3(position));
            }
            const auto center = lm::center(position_bbox);

            for (auto& position : content._position_data)
            {
                position = lm::vec3(lm::dvec3(position) - center);
            }

            subtile->rtc_transform = lm::translation(center);
        }
    }
    else if (parser.has_semantic("POSITION_QUANTIZED"))
    {
        const auto quantized_volume_offset = parser.get_global_dvec3("QUANTIZED_VOLUME_OFFSET");
        const auto quantized_volume_scale = parser.get_global_dvec3("QUANTIZED_VOLUME_SCALE");

        if (!quantized_volume_offset.has_value() || !quantized_volume_scale.has_value())
        {
            HRZ_LOG_ERROR(
                "Missing either QUANTIZED_VOLUME_OFFSET or QUANTIZED_VOLUME_SCALE in i3dm tile");
            return false;
        }

        // The offset is merged with RTC_CENTER rather than being added to the
        // positions in single precision on the GPU (see the POSITION case above).
        subtile->rtc_transform =
            subtile->rtc_transform * lm::translation(lm::dvec3(quantized_volume_offset.value()));
        content._quantized_volume_scale = quantized_volume_scale.value();

        if (!parser.copy_data(
                "POSITION_QUANTIZED", content._instances_length, content._position_quantized_data))
        {
            HRZ_LOG_ERROR("Could not get POSITION_QUANTIZED data from feature table");
            return false;
        }
    }
    else
    {
        HRZ_LOG_ERROR("I3dm feature table is missing features positions");
        return false;
    }

    if (parser.has_semantic("NORMAL_UP") && parser.has_semantic("NORMAL_RIGHT"))
    {
        content._use_east_north_up_orientation = false;

        const auto normal_up_data_opt =
            parser.get_data_span<lm::vec3>("NORMAL_UP", content._instances_length);
        const auto normal_right_data_opt =
            parser.get_data_span<lm::vec3>("NORMAL_RIGHT", content._instances_length);

        if (!normal_up_data_opt.has_value() || !normal_right_data_opt.has_value())
        {
            HRZ_LOG_ERROR("Could not get NORMAL_UP or NORMAL_RIGHT data from feature table");
            return false;
        }

        const auto normal_up_data = normal_up_data_opt.value();
        const auto normal_right_data = normal_right_data_opt.value();

        // We interleave the normals
        content._normal_data.resize(2 * content._instances_length);
        for (uint32_t i = 0; i < content._instances_length; ++i)
        {
            content._normal_data[2 * i + 0] = normal_right_data[i];
            content._normal_data[2 * i + 1] = normal_up_data[i];
        }
    }
    else if (parser.has_semantic("NORMAL_UP_OCT32P") && parser.has_semantic("NORMAL_RIGHT_OCT32P"))
    {
        content._use_east_north_up_orientation = false;

        const auto normal_up_oct32p_data_opt =
            parser.get_data_span<lm::usvec2>("NORMAL_UP_OCT32P", content._instances_length);
        const auto normal_right_oct32p_data_opt =
            parser.get_data_span<lm::usvec2>("NORMAL_RIGHT_OCT32P", content._instances_length);

        if (!normal_up_oct32p_data_opt.has_value() || !normal_right_oct32p_data_opt.has_value())
        {
            HRZ_LOG_ERROR(
                "Could not get NORMAL_UP_OCT32P or NORMAL_RIGHT_OCT32P data from feature table");
            return false;
        }

        const auto normal_up_oct32p_data = normal_up_oct32p_data_opt.value();
        const auto normal_right_oct32p_data = normal_right_oct32p_data_opt.value();

        // We interleave the normals
        content._normal_oct32p_data.resize(content._instances_length);
        for (uint32_t i = 0; i < content._instances_length; ++i)
        {
            content._normal_oct32p_data[i].xy = normal_right_oct32p_data[i];
            content._normal_oct32p_data[i].zw = normal_up_oct32p_data[i];
        }
    }

    if (parser.has_semantic("SCALE_NON_UNIFORM"))
    {
        if (!parser.copy_data("SCALE_NON_UNIFORM", content._instances_length, content._scale_data))
        {
            HRZ_LOG_ERROR("Could not get SCALE_NON_UNIFORM data from feature table");
            return false;
        }
    }
    else
    {
        // Use a default uniform scale of 1.
        content._scale_data.resize(content._instances_length, lm::vec3{1});
    }

    if (parser.has_semantic("SCALE"))
    {
        const auto scale_data_opt = parser.get_data_span<float>("SCALE", content._instances_length);

        if (!scale_data_opt.has_value())
        {
            HRZ_LOG_ERROR("Could not get SCALE data from feature table");
            return false;
        }

        const auto scale_data = scale_data_opt.value();

        for (uint32_t i = 0; i < content._instances_length; ++i)
        {
            content._scale_data[i] *= scale_data[i];
        }
    }

    content._batch_id_data.resize(content._instances_length);

    if (parser.has_semantic("BATCH_ID"))
    {
        const auto component_type = parser.get_semantic_component_type(
            "BATCH_ID", hrz::three_d_tiles::AttributeComponentType::UNSIGNED_SHORT);

        switch (component_type)
        {
            case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_BYTE:
            {
                const auto data =
                    parser.get_data_span<uint8_t>("BATCH_ID", content._instances_length);
                if (!data.has_value())
                {
                    HRZ_LOG_ERROR("Could not get BATCH_ID data from feature table");
                    return false;
                }
                std::ranges::copy_n(
                    data.value().begin(), content._instances_length,
                    content._batch_id_data.begin());
                break;
            }
            case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_SHORT:
            {
                const auto data =
                    parser.get_data_span<uint16_t>("BATCH_ID", content._instances_length);
                if (!data.has_value())
                {
                    HRZ_LOG_ERROR("Could not get BATCH_ID data from feature table");
                    return false;
                }
                std::ranges::copy_n(
                    data.value().begin(), content._instances_length,
                    content._batch_id_data.begin());
                break;
            }
            case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_INT:
            {
                const auto data =
                    parser.get_data_span<uint32_t>("BATCH_ID", content._instances_length);
                if (!data.has_value())
                {
                    HRZ_LOG_ERROR("Could not get BATCH_ID data from feature table");
                    return false;
                }
                std::ranges::copy_n(
                    data.value().begin(), content._instances_length,
                    content._batch_id_data.begin());
                break;
            }
            default:
                HRZ_LOG_ERROR(
                    "Unexpected component type '{}' for 'BATCH_ID' semantic in i3dm tile",
                    (int)component_type);
                return false;
        }

        subtile->batch_length = *std::ranges::max_element(content._batch_id_data) + 1;
    }
    else
    {
        // Not defined in the spec but Cesium's implementation says:
        // "If BATCH_ID semantic is undefined, batchId is just the instance number"
        // https://github.com/pmconne/cesium/blob/a4be986c04b863e97e251c3fc6b78f106f7de4d7/Source/Scene/Instanced3DModel3DTileContent.js#L433
        std::iota(std::begin(content._batch_id_data), std::end(content._batch_id_data), 0);
        subtile->batch_length = content._instances_length;
    }

    return true;
}

std::optional<ThreeDTile::Subtile> ThreeDTile::I3dmContent::load_content(
    const ContentWorkContext& ctx,
    const Tileset& tileset,
    const ThreeDTile& tile,
    const hrz::blobs::BlobHandle& tile_data_blob,
    const ThreeDTile::SubtileIdentification& ident,
    ModelCache& model_cache)
{
    const auto tile_data = tile_data_blob.get_data();
    const size_t tile_data_size = tile_data.size();

    const auto magic = tile_data.subspan(0, 4);

    if (std::strncmp((const char*)magic.data(), "i3dm", 4) != 0) return std::nullopt;

    const auto header_size = 32;

    if (tile_data_size < header_size)
    {
        HRZ_LOG_ERROR(
            "Not enough data received for i3dm tile \"{}\": {}", tile.uri, tile_data_size);
        return std::nullopt;
    }

    const auto header_data = tile_data.subspan(0, 32);

    const auto format_version = hrz::read_le_unsafe<uint32_t>(header_data, 4);
    if (format_version != 1)
    {
        HRZ_LOG_ERROR("Unsupported i3dm version in tile \"{}\": {}", tile.uri, format_version);
        return std::nullopt;
    }

    const auto declared_size = hrz::read_le_unsafe<uint32_t>(header_data, 8);
    if (declared_size != tile_data_size)
    {
        HRZ_LOG_ERROR(
            "Wrong i3dm size in tile \"{}\": Expected {}, received {}", tile.uri, declared_size,
            tile_data_size);
        return std::nullopt;
    }

    const auto feature_table_json_size = hrz::read_le_unsafe<uint32_t>(header_data, 12);
    const auto feature_table_bin_size = hrz::read_le_unsafe<uint32_t>(header_data, 16);
    const auto batch_table_json_size = hrz::read_le_unsafe<uint32_t>(header_data, 20);
    const auto batch_table_bin_size = hrz::read_le_unsafe<uint32_t>(header_data, 24);
    const auto gltf_format = hrz::read_le_unsafe<uint32_t>(header_data, 28);

    const auto gltf_offset = header_size + feature_table_json_size + feature_table_bin_size
        + batch_table_json_size + batch_table_bin_size;

    if (header_size + feature_table_json_size + feature_table_bin_size > tile_data_size)
    {
        HRZ_LOG_ERROR("Invalid i3dm header in tile \"{}\"", tile.uri);
        return std::nullopt;
    }

    const auto* config = tileset.config;
    const hrz::monitoring::ResourceOwner owner{
        hrz::monitoring::systems::ThreeDTilesLayers, config->global_layer_id
    };

    ThreeDTile::Subtile subtile;
    auto& content = subtile.content.emplace<ThreeDTile::I3dmContent>();

    const auto feature_table_json_data = tile_data.subspan(header_size, feature_table_json_size);
    const auto feature_table_bin_blob = tile_data_blob.make_sub_blob(
        hrz::unsafe("File size has been checked above"), header_size + feature_table_json_size,
        feature_table_bin_size);
    if (!decode_i3dm_feature_table(&subtile, feature_table_json_data, feature_table_bin_blob))
    {
        HRZ_LOG_ERROR("Could not decode feature table of i3dm tile \"{}\"", tile.uri);
        return std::nullopt;
    }

    {
        hrz_jobs::EncodedBatchTable params;
        params.batch_length = subtile.batch_length;
        params.json_data = hrz::blobs::make_sub_blob(
            hrz::unsafe("Component sizes are checked against tile size"), ctx.ba, tile_data_blob,
            header_size + feature_table_json_size + feature_table_bin_size, batch_table_json_size);
        params.bin_data = hrz::blobs::make_sub_blob(
            hrz::unsafe("Component sizes are checked against tile size"), ctx.ba, tile_data_blob,
            header_size + feature_table_json_size + feature_table_bin_size + batch_table_json_size,
            batch_table_bin_size);
        params.attributes = config->attributes;
        subtile.decode_batch_table_ticket =
            hrz_jobs::add_job_decode_three_d_tiles_batch_table(ctx.js, std::move(params), owner);
    }

    content._object_reference = ident.object_reference;
    content._feature_reference = ident.feature_reference;

    content._draw_prps = config->inherited_draw_prps;
    content._draw_prps.transform = get_axis_swap_transform(tileset.gltf_up_axis);

    if (gltf_format == 1) // Embedded glTF
    {
        const auto gltf_span = tile_data.subspan(gltf_offset, tile_data.size() - gltf_offset);
        const auto gltf_size = hrz::model::fetch_glb_declared_size(gltf_span);

        if (!gltf_size.has_value() || *gltf_size > gltf_span.size())
        {
            HRZ_LOG_ERROR("Invalid embedded glTF size in i3dm tile \"{}\"", tile.uri);
            return std::nullopt;
        }

        const auto gltf_blob = hrz::blobs::make_sub_blob(
            hrz::unsafe("Offset and size are checked above"), ctx.ba, tile_data_blob, gltf_offset,
            *gltf_size);

        content._model_uri_hash = std::nullopt;
        content._prototype = hrz::model::create_from_gltf_blob(
            ctx.ba, ctx.attributions, gltf_blob, tile.uri, tileset.base_url.derive_base(tile.uri),
            gltf_offset, config->headers, config->additional_attribution,
            config->get_request_queue(),
            hrz::combine_loading_priorities(
                config->loading_priority, std::numeric_limits<uint16_t>::max()),
            owner);
    }
    else if (gltf_format == 0) // External glTF
    {
        const hrz::BaseUrl i3dm_base_url = tileset.base_url.derive_base(tile.uri);
        const auto gltf_blob = hrz::blobs::make_sub_blob(
            hrz::unsafe("Component sizes are checked against tile size"), ctx.ba, tile_data_blob,
            gltf_offset);
        const auto gltf_data = gltf_blob.get_data();
        const std::string_view partial_uri =
            hrz::str::rtrim_s({(const char*)gltf_data.data(), gltf_data.size()}, '\x20');
        const std::string full_uri = i3dm_base_url.derive(partial_uri);
        const uint32_t request_priority = hrz::combine_loading_priorities(
            config->loading_priority, std::numeric_limits<uint16_t>::max());

        content._model_uri_hash = model_cache.ref(
            full_uri, i3dm_base_url, config->headers, config->additional_attribution,
            config->get_request_queue(), request_priority, owner);
    }
    else
    {
        HRZ_LOG_ERROR("Invalid glTF format specifier in i3dm tile \"{}\"", tile.uri);
        return std::nullopt;
    }

    const size_t color_count = std::max((uint32_t)1, content._instances_length);
    // Fill the colours with pure white, so that the styling has no effect so far.
    content._per_instance_color.resize(color_count, lm::ubvec4{0xff});

    return std::move(subtile);
}

void ThreeDTile::I3dmContent::update_appearance(
    const hrz::model::DrawProperties& inherited_draw_prps)
{
    _draw_prps.clip_id = inherited_draw_prps.clip_id;
    _draw_prps.lighting = inherited_draw_prps.lighting;
    _draw_prps.draw_under_flat_overlays = inherited_draw_prps.draw_under_flat_overlays;
}

void ThreeDTile::I3dmContent::update_material_properties(
    const hrz::model::DrawProperties& inherited_draw_prps)
{
    _draw_prps.feature_color_blend_mode = inherited_draw_prps.feature_color_blend_mode;
    _draw_prps.feature_color_blend_strength = inherited_draw_prps.feature_color_blend_strength;
}

void ThreeDTile::I3dmContent::work(const ContentWorkContext& ctx)
{
    if (_prototype)
    {
        hrz::model::work(_prototype, ctx.al, ctx.js, ctx.ba, ctx.imgdec, ctx.attributions);

        if (_model)
        {
            hrz::model::work(_prototype, *_model);
        }
    }
}

void ThreeDTile::I3dmContent::create_instance_group(
    const ThreeDTile& tile,
    const ThreeDTile::Subtile& subtile,
    const TilesetConfig* tileset_config)
{
    if (!_model_uri_hash.has_value())
    {
        _geometry = {hrz::model::create_instanced_model_geometry(_prototype)};
        _material = {hrz::model::create_model_material(_prototype, {})};
        _model = {hrz::model::create_baked_model(_prototype, *_geometry, *_material)};
    }

    const bool use_compressed_normals = !_normal_oct32p_data.empty() && _normal_data.empty();
    const bool use_compressed_positions =
        !_position_quantized_data.empty() && _position_data.empty();

    hrz::model::InstanceGroupData group_data;
    group_data.use_enu_orientation = _use_east_north_up_orientation;
    group_data.transform = tile.base_transform * subtile.rtc_transform;
    group_data.cull_modifier = my::CullModifier::Swap;
    group_data.scales = _scale_data;
    group_data.colors = _per_instance_color;
    group_data.object_ids = _batch_id_data;
    group_data.feature_id_per_object =
        subtile.batches_to_feature_ids.hashes().get_data().unsafe_as_span();

    if (use_compressed_positions)
    {
        group_data.compressed_positions = _position_quantized_data;
        group_data.position_compression.type = hrz::model::DracoCompressionType::Quantized;
        // The quantized volume offset is already in the group transform.
        group_data.position_compression.quantization_mins = lm::vec3(0.0F);
        group_data.position_compression.quantization_scale =
            lm::vec3(_quantized_volume_scale) / 65535.0F;
    }
    else
    {
        group_data.positions = _position_data;
        group_data.position_compression.type = hrz::model::DracoCompressionType::None;
    }

    if (use_compressed_normals)
    {
        group_data.compressed_normals = _normal_oct32p_data;
        group_data.normal_compression.type = hrz::model::DracoCompressionType::OctEncoded;
        group_data.normal_compression.quantization_scale = lm::vec3(2.0F / 65535.0F);
    }
    else
    {
        group_data.normals = _normal_data;
        group_data.normal_compression.type = hrz::model::DracoCompressionType::None;
    }

    _instance_group = hrz::model::create_instance_group(
        _prototype, _object_reference, _feature_reference, subtile.batch_id_offset, group_data);

    hrz::model::set_instance_group_selection(
        _prototype, _instance_group.value(), tileset_config->selected_feature_ids);

    _position_data.clear();
    _position_data.shrink_to_fit();
    _position_quantized_data.clear();
    _position_quantized_data.shrink_to_fit();
    _normal_data.clear();
    _normal_data.shrink_to_fit();
    _normal_oct32p_data.clear();
    _normal_oct32p_data.shrink_to_fit();
    _scale_data.clear();
    _scale_data.shrink_to_fit();

    // Don't clear batch_id_data, we need it to expand the per batch colors
    // to per instance colors after styling!
}

ThreeDTile::ContentLoadStatusUpdate ThreeDTile::I3dmContent::work_loading(
    const ContentWorkContext& ctx,
    const ThreeDTile& tile,
    const ThreeDTile::Subtile& subtile,
    const TilesetConfig* tileset_config)
{
    if (_prototype == nullptr && _model_uri_hash.has_value())
    {
        const auto status = ctx.model_cache->get_model_status(_model_uri_hash.value());
        if (!status.has_value() || *status == ModelCache::ModelStatus::ERROR)
        {
            return ContentLoadStatusUpdate::ERROR;
        }
        else if (*status == ModelCache::ModelStatus::LOADED)
        {
            const auto parts = ctx.model_cache->get_model_parts(_model_uri_hash.value());
            if (!parts.has_value())
            {
                return ContentLoadStatusUpdate::ERROR;
            }

            _prototype = parts->prototype;
            _geometry = {parts->geometry};
            _material = {parts->material};
            _model = {parts->model};
        }
    }
    else if (subtile.batch_table_decoded && !_instance_group.has_value())
    {
        create_instance_group(tile, subtile, tileset_config);
    }

    if (!_instance_group.has_value()) return ContentLoadStatusUpdate::STILL_LOADING;

    // The statuses below are advanced by `work_gpu`.
    const auto prototype_status = hrz::model::get_status(_prototype);
    const auto model_status = hrz::model::get_status(_prototype, *_model);
    const auto group_status =
        hrz::model::get_instance_group_status(_prototype, _instance_group.value());

    if (group_status == hrz::model::InstanceGroupStatus::Ready
        && prototype_status == hrz::model::ModelPrototypeStatus::Ready
        && model_status == hrz::model::BakedModelStatus::Ready)
    {
        return ContentLoadStatusUpdate::LOADED;
    }
    else if (
        group_status == hrz::model::InstanceGroupStatus::Error
        || prototype_status == hrz::model::ModelPrototypeStatus::Error
        || model_status == hrz::model::BakedModelStatus::Error)
    {
        return ContentLoadStatusUpdate::ERROR;
    }

    return ContentLoadStatusUpdate::STILL_LOADING;
}

hrz::RenderRequest ThreeDTile::I3dmContent::work_gpu(
    const ContentWorkContext& ctx,
    const ContentRenderContext& render_ctx,
    const ThreeDTile& /* tile */,
    const ThreeDTile::Subtile& /* subtile */,
    const TilesetConfig* /* tileset_config */)
{
    if (_prototype == nullptr) return {};

    hrz::RenderRequest render_request;

    hrz::model::work_gpu(_prototype, ctx.ba, render_ctx.render);

    if (_model)
    {
        render_request |= hrz::model::work_gpu(
            _prototype, *_model, render_ctx.model_shared_resources_instanced, render_ctx.render);
    }

    if (_instance_group.has_value())
    {
        hrz::model::work_gpu(_prototype, _instance_group.value(), render_ctx.render);
    }

    return render_request;
}

void ThreeDTile::I3dmContent::draw(
    const ContentWorkContext& ctx,
    const ContentRenderContext& render_ctx,
    hrz::SceneViewBitset draw_in)
{
    if (_prototype && _model.has_value())
    {
        hrz::model::draw(
            _prototype, _model.value(), _instance_group.value(), _draw_prps, draw_in.bits(),
            render_ctx.model_shared_resources_instanced, render_ctx.render, ctx.attributions);
    }
}

void ThreeDTile::I3dmContent::unload(
    const ContentWorkContext& ctx,
    std::vector<my::ResourceHandle>& to_destroy)
{
    if (_prototype && _instance_group)
    {
        hrz::model::destroy(_prototype, *_instance_group);
        _instance_group = std::nullopt;
    }

    if (_model_uri_hash.has_value())
    {
        ctx.model_cache->unref(_model_uri_hash.value());
        _model_uri_hash = std::nullopt;
    }
    else if (_prototype)
    {
        // Non-external models are not put in the cache, free them directly

        if (_geometry)
        {
            hrz::model::destroy(_prototype, *_geometry);
        }

        if (_model)
        {
            hrz::model::destroy(_prototype, *_model);
        }

        if (_material)
        {
            hrz::model::destroy(_prototype, *_material);
        }

        hrz::model::destroy(_prototype, ctx.al, ctx.js, ctx.ba, to_destroy);
    }

    _geometry = std::nullopt;
    _model = std::nullopt;
    _material = std::nullopt;
    _prototype = nullptr;
    _instances_length = 0;

    _position_data.clear();
    _position_data.shrink_to_fit();
    _position_quantized_data.clear();
    _position_quantized_data.shrink_to_fit();
    _normal_data.clear();
    _normal_data.shrink_to_fit();
    _normal_oct32p_data.clear();
    _normal_oct32p_data.shrink_to_fit();
    _scale_data.clear();
    _scale_data.shrink_to_fit();
    _batch_id_data.clear();
    _batch_id_data.shrink_to_fit();
    _per_instance_color.clear();
    _per_instance_color.shrink_to_fit();
}

void ThreeDTile::I3dmContent::reset_style()
{
    std::ranges::fill(_per_instance_color, lm::ubvec4{0xff});
}

void ThreeDTile::I3dmContent::update_selection(
    const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_feature_ids)
{
    if (_prototype && _instance_group.has_value())
    {
        hrz::model::set_instance_group_selection(
            _prototype, _instance_group.value(), selected_feature_ids);
    }
}

void ThreeDTile::I3dmContent::update_feature_colors(
    std::span<const lm::ubvec4> per_batch_colors,
    bool /* has_transparent_color */)
{
    assert(_prototype && _instance_group);

    if (!per_batch_colors.empty())
    {
        for (uint32_t inst = 0; inst < _per_instance_color.size(); ++inst)
        {
            if (inst < _batch_id_data.size())
            {
                const uint32_t batch_id = _batch_id_data[inst];
                if (batch_id < per_batch_colors.size())
                {
                    _per_instance_color[inst] = per_batch_colors[batch_id];
                }
            }
            else
            {
                _per_instance_color[inst] = per_batch_colors[0];
            }
        }
    }

    hrz::model::set_instance_group_colors(_prototype, *_instance_group, _per_instance_color);
}

} // namespace hrz::three_d_tiles
