// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/profiling.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/three_d_tiles_jobs_params.h"
#include "hrz/core/loading_priorities.h"
#include "hrz/core/model/materials_manager.h"
#include "hrz/core/model/model.h"
#include "hrz/core/three_d_tiles/feature_table_parser.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/fnd/log.h"

#include <cassert>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace hrz::three_d_tiles
{

bool ThreeDTile::B3dmContent::decode_b3dm_feature_table(
    ThreeDTile::Subtile* subtile,
    std::span<const std::byte> feature_table_json_data,
    const hrz::blobs::BlobHandle& feature_table_bin_blob)
{
    HRZ_SCOPED_SAMPLE("decode b3dm feature table");

    if (feature_table_json_data.empty())
    {
        return true;
    }

    auto parser_opt = hrz::three_d_tiles::FeatureTableParser::make(
        feature_table_json_data, feature_table_bin_blob);
    if (!parser_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not create feature table parser");
        return false;
    }

    const auto parser = std::move(parser_opt.value());

    if (const auto batch_length = parser.get_global_uint32("BATCH_LENGTH");
        batch_length.has_value())
    {
        subtile->batch_length = batch_length.value();
    }
    else
    {
        HRZ_LOG_ERROR("Could not get BATCH_LENGTH from feature table");
        return false;
    }

    const lm::dvec3 rtc_center = parser.get_global_dvec3("RTC_CENTER").value_or(lm::dvec3{});
    subtile->rtc_transform = lm::translation(rtc_center);

    return true;
}

void ThreeDTile::B3dmContent::load_gltf_blob(
    const ContentWorkContext& ctx,
    const Tileset& tileset,
    const ThreeDTile& tile,
    ThreeDTile::Subtile& subtile,
    const hrz::blobs::BlobHandle& gltf_blob,
    size_t gltf_offset,
    const ThreeDTile::SubtileIdentification& ident)
{
    const auto* config = tileset.config;
    auto& content = std::get<ThreeDTile::B3dmContent>(subtile.content);

    content._object_reference = ident.object_reference;
    content._feature_reference = ident.feature_reference;

    content._draw_prps = config->inherited_draw_prps;
    content._draw_prps.transform =
        tile.base_transform * subtile.rtc_transform * get_axis_swap_transform(tileset.gltf_up_axis);

    content._prototype = hrz::model::create_from_gltf_blob(
        ctx.ba, ctx.attributions, gltf_blob, tile.uri, tileset.base_url.derive_base(tile.uri),
        gltf_offset, config->headers, config->additional_attribution, config->get_request_queue(),
        hrz::combine_loading_priorities(
            config->loading_priority, std::numeric_limits<uint16_t>::max()),
        {hrz::monitoring::systems::ThreeDTilesLayers, config->global_layer_id});

    content._materials.prototype_may_have_been_recreated(content._prototype);
    content._materials.recreate_all_materials(content._prototype, config->materials);
    content._materials.set_active_materials(
        config->base_material,
        content._draw_prps.overlay_material_enabled
            ? config->overlay_material
            : std::optional<std::string_view>(std::nullopt));
}

std::optional<ThreeDTile::Subtile> ThreeDTile::B3dmContent::load_content(
    const ContentWorkContext& ctx,
    const Tileset& tileset,
    const ThreeDTile& tile,
    const hrz::blobs::BlobHandle& tile_data_blob,
    const ThreeDTile::SubtileIdentification& ident)
{
    const auto tile_data = tile_data_blob.get_data();
    const size_t tile_data_size = tile_data.size();

    const auto magic = tile_data.subspan(0, 4);

    if (std::strncmp((const char*)magic.data(), "b3dm", 4) != 0) return std::nullopt;

    const auto header_size = 28;

    if (tile_data_size < header_size)
    {
        HRZ_LOG_ERROR(
            "Not enough data received for b3dm tile \"{}\": {}", tile.uri, tile_data_size);
        return std::nullopt;
    }

    const auto header_data = tile_data.subspan(0, 28);

    const auto format_version = hrz::read_le_unsafe<uint32_t>(header_data, 4);
    if (format_version != 1)
    {
        HRZ_LOG_ERROR("Unsupported b3dm version in tile \"{}\": {}", tile.uri, format_version);
        return std::nullopt;
    }

    const auto declared_size = hrz::read_le_unsafe<uint32_t>(header_data, 8);
    if (declared_size != tile_data_size)
    {
        HRZ_LOG_ERROR(
            "Wrong b3dm size in tile \"{}\": Expected {}, received {}", tile.uri, declared_size,
            tile_data_size);
        return std::nullopt;
    }

    const auto feature_table_json_size = hrz::read_le_unsafe<uint32_t>(header_data, 12);
    const auto feature_table_bin_size = hrz::read_le_unsafe<uint32_t>(header_data, 16);
    const auto batch_table_json_size = hrz::read_le_unsafe<uint32_t>(header_data, 20);
    const auto batch_table_bin_size = hrz::read_le_unsafe<uint32_t>(header_data, 24);
    const auto gltf_offset = header_size + feature_table_json_size + feature_table_bin_size
        + batch_table_json_size + batch_table_bin_size;

    if (gltf_offset > tile_data_size)
    {
        HRZ_LOG_ERROR("Invalid b3dm header in tile \"{}\"", tile.uri);
        return std::nullopt;
    }

    if (header_size + feature_table_json_size + feature_table_bin_size > tile_data_size)
    {
        HRZ_LOG_ERROR("Invalid b3dm header in tile \"{}\"", tile.uri);
        return std::nullopt;
    }

    const auto* config = tileset.config;

    ThreeDTile::Subtile subtile;
    subtile.content.emplace<ThreeDTile::B3dmContent>();

    const auto feature_table_json_data = tile_data.subspan(header_size, feature_table_json_size);
    const auto feature_table_bin_blob = tile_data_blob.make_sub_blob(
        hrz::unsafe("File size has been checked above"), header_size + feature_table_json_size,
        feature_table_bin_size);
    if (!decode_b3dm_feature_table(&subtile, feature_table_json_data, feature_table_bin_blob))
    {
        HRZ_LOG_ERROR("Could not decode feature table of b3dm tile \"{}\"", tile.uri);
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
        subtile.decode_batch_table_ticket = hrz_jobs::add_job_decode_three_d_tiles_batch_table(
            ctx.js, std::move(params),
            {hrz::monitoring::systems::ThreeDTilesLayers, config->global_layer_id});
    }

    const auto gltf_blob = hrz::blobs::make_sub_blob(
        hrz::unsafe("Component sizes are checked against tile size"), ctx.ba, tile_data_blob,
        gltf_offset);

    load_gltf_blob(ctx, tileset, tile, subtile, gltf_blob, gltf_offset, ident);

    return std::move(subtile);
}

std::optional<ThreeDTile::Subtile> ThreeDTile::B3dmContent::load_gltf_content(
    const ContentWorkContext& ctx,
    const Tileset& tileset,
    const ThreeDTile& tile,
    const hrz::blobs::BlobHandle& gltf_blob,
    const ThreeDTile::SubtileIdentification& ident)
{
    const auto* config = tileset.config;

    ThreeDTile::Subtile subtile;
    subtile.content.emplace<ThreeDTile::B3dmContent>();

    {
        hrz_jobs::EncodedBatchTable params;
        params.batch_length = subtile.batch_length;
        params.json_data = {};
        params.bin_data = {};
        params.attributes = config->attributes;
        subtile.decode_batch_table_ticket = hrz_jobs::add_job_decode_three_d_tiles_batch_table(
            ctx.js, std::move(params),
            {hrz::monitoring::systems::ThreeDTilesLayers, config->global_layer_id});
    }

    load_gltf_blob(ctx, tileset, tile, subtile, gltf_blob, 0, ident);

    return std::move(subtile);
}

void ThreeDTile::B3dmContent::update_appearance(
    const hrz::model::DrawProperties& inherited_draw_prps)
{
    _draw_prps.clip_id = inherited_draw_prps.clip_id;
    _draw_prps.lighting = inherited_draw_prps.lighting;
    _draw_prps.draw_under_flat_overlays = inherited_draw_prps.draw_under_flat_overlays;
}

void ThreeDTile::B3dmContent::update_material_properties(
    const hrz::model::DrawProperties& inherited_draw_prps)
{
    _draw_prps.feature_color_blend_mode = inherited_draw_prps.feature_color_blend_mode;
    _draw_prps.feature_color_blend_strength = inherited_draw_prps.feature_color_blend_strength;
    _draw_prps.apply_feature_color_to_overlay = inherited_draw_prps.apply_feature_color_to_overlay;
    _draw_prps.overlay_material_enabled = inherited_draw_prps.overlay_material_enabled;
    _draw_prps.overlay_material_opacity = inherited_draw_prps.overlay_material_opacity;
}

void ThreeDTile::B3dmContent::recreate_all_materials(
    std::span<const hrz_proto::Material> new_materials)
{
    if (_prototype)
    {
        _materials.recreate_all_materials(_prototype, new_materials);
    }
}

void ThreeDTile::B3dmContent::add_material(const hrz_proto::Material& material)
{
    if (_prototype)
    {
        _materials.add_material(material);
    }
}

void ThreeDTile::B3dmContent::remove_material(size_t material_index)
{
    assert(material_index < _materials.material_count());

    if (_prototype)
    {
        _materials.remove_material(_prototype, material_index);
    }
}

void ThreeDTile::B3dmContent::update_material(
    size_t material_index,
    const hrz_proto::Material& material)
{
    if (_prototype)
    {
        _materials.update_whole_material(_prototype, material, material_index);
    }
}

void ThreeDTile::B3dmContent::update_material_palette(
    size_t material_index,
    const hrz_proto::NumericPalette& palette)
{
    if (_prototype)
    {
        _materials.update_material_palette(_prototype, palette, material_index);
    }
}

void ThreeDTile::B3dmContent::update_active_materials(const TilesetConfig& tileset_config)
{
    if (_prototype)
    {
        _materials.set_active_materials(
            tileset_config.base_material,
            tileset_config.inherited_draw_prps.overlay_material_enabled
                ? tileset_config.overlay_material
                : std::optional<std::string_view>(std::nullopt));
    }
}

void ThreeDTile::B3dmContent::work(const ContentWorkContext& ctx)
{
    if (_prototype)
    {
        hrz::model::work(_prototype, ctx.al, ctx.js, ctx.ba, ctx.imgdec, ctx.attributions);
        _materials.work(_prototype);
    }
}

ThreeDTile::ContentLoadStatusUpdate ThreeDTile::B3dmContent::work_loading(
    const ContentWorkContext& /* ctx */,
    const ThreeDTile& /* tile */,
    const ThreeDTile::Subtile& subtile,
    const TilesetConfig* tileset_config)
{
    if (subtile.batch_table_decoded && !_geometry)
    {
        // @Safety the lifetime of the hashes blob array data is
        // extended to the end of the create_batched_model_geometry,
        // which copies the content without keeping a reference to it.
        _geometry = {hrz::model::create_batched_model_geometry(
            _prototype, _object_reference, _feature_reference, subtile.batch_id_offset,
            subtile.batch_length,
            subtile.batches_to_feature_ids.hashes().get_data().unsafe_as_span())};

        hrz::model::set_batched_selection(
            _prototype, *_geometry, tileset_config->selected_feature_ids);
    }

    if (!_prototype || !_geometry) return ContentLoadStatusUpdate::STILL_LOADING;

    _materials.update(_prototype, *_geometry);

    const auto active_model = _materials.get_active_model();
    if (!active_model) return ContentLoadStatusUpdate::STILL_LOADING;

    const auto status = hrz::model::get_status(_prototype, active_model.value());
    if (status == hrz::model::BakedModelStatus::Ready)
    {
        return ContentLoadStatusUpdate::LOADED;
    }
    else if (status == hrz::model::BakedModelStatus::ReadyWithErrors)
    {
        HRZ_LOG_WARNING("Couldn't properly load material for b3dm tile");
        return ContentLoadStatusUpdate::LOADED;
    }
    else if (status == hrz::model::BakedModelStatus::Error)
    {
        return ContentLoadStatusUpdate::ERROR;
    }

    return ContentLoadStatusUpdate::STILL_LOADING;
}

hrz::RenderRequest ThreeDTile::B3dmContent::work_gpu(
    const ContentWorkContext& ctx,
    const ContentRenderContext& render_ctx,
    const ThreeDTile& /* tile */,
    const ThreeDTile::Subtile& /* subtile */,
    const TilesetConfig* /* tileset_config */)
{
    if (!_prototype) return {};

    hrz::model::work_gpu(_prototype, ctx.ba, render_ctx.render);
    return _materials.work_gpu(_prototype, render_ctx.model_shared_resources, render_ctx.render);
}

void ThreeDTile::B3dmContent::draw(
    const ContentWorkContext& ctx,
    const ContentRenderContext& render_ctx,
    hrz::SceneViewBitset draw_in)
{
    const auto& model = _materials.get_active_model();
    if (_prototype && model.has_value())
    {
        hrz::model::draw(
            _prototype, model.value(), _draw_prps, draw_in.bits(),
            render_ctx.model_shared_resources, render_ctx.render, ctx.attributions);
    }
}

void ThreeDTile::B3dmContent::unload(
    const ContentWorkContext& ctx,
    std::vector<my::ResourceHandle>& to_destroy)
{
    if (_prototype)
    {
        _materials.delete_all(_prototype);
        if (_geometry)
        {
            hrz::model::destroy(_prototype, *_geometry);
        }
        hrz::model::destroy(_prototype, ctx.al, ctx.js, ctx.ba, to_destroy);
        _prototype = nullptr;
    }
}

void ThreeDTile::B3dmContent::update_selection(
    const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_feature_ids)
{
    if (_prototype && _geometry)
    {
        hrz::model::set_batched_selection(_prototype, *_geometry, selected_feature_ids);
    }
}

void ThreeDTile::B3dmContent::update_feature_colors(
    std::span<const lm::ubvec4> per_batch_colors,
    bool /* has_transparent_color */)
{
    assert(_prototype && _geometry);

    // There is at least one colour: if the tile does not have
    // batch IDs, the whole geometry is treated as batch 0.
    hrz::model::set_batched_colors(_prototype, *_geometry, per_batch_colors);
}

} // namespace hrz::three_d_tiles
