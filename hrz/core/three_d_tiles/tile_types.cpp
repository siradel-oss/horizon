// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/three_d_tiles/tile_types.h"

#include "hrz/common/maths.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/core/job_scheduler.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/three_d_tiles_jobs_params.h"
#include "hrz/core/render/context.h"

#include <algorithm>
#include <cassert>

namespace hrz::three_d_tiles
{

void ThreeDTile::add_loaded_subtile(
    const Tileset& tileset,
    Subtile&& subtile,
    SubtileIdentification* subtile_ident)
{
    const auto* config = tileset.config;

    subtile.vector_data_attribute_request_id =
        VectorDataRequestId{
            .tileset_handle = tileset.vector_data_attribute_request_id,
            .tile_index = index,
            .subtile_index = (uint32_t)subtiles.size(),
        }
            .encode();

    subtile.attribute_values.reserve(config->attributes.size());
    for (unsigned int i = 0; i < config->attributes.size(); ++i)
    {
        subtile.attribute_values.emplace_back(std::nullopt);
    }

    subtile.batch_id_offset = subtile_ident->batch_id_offset;
    subtile_ident->batch_id_offset += subtile.batch_length;

    // If the batch length is 0, all the geometry takes on the same style.
    const size_t color_count = std::max((uint32_t)1, subtile.batch_length);
    subtile.per_batch_color.resize(color_count, lm::ubvec4{0xff});
    subtile.has_transparent_batch = false;

    assert(subtile._load_status == Subtile::LoadStatus::LOADING);
    subtiles.push_back(std::move(subtile));
    _subtiles_left_to_load += 1;
}

void ThreeDTile::set_subtile_load_status(Subtile& subtile, Subtile::LoadStatus status)
{
    assert(
        (subtile._load_status == Subtile::LoadStatus::LOADING)
        != (status == Subtile::LoadStatus::LOADING));

    if (subtile._load_status == Subtile::LoadStatus::LOADING) _subtiles_left_to_load -= 1;
    subtile._load_status = status;
    if (subtile._load_status == Subtile::LoadStatus::LOADING) _subtiles_left_to_load += 1;
}

#if HRZ_DEBUG
void ThreeDTile::check_invariants() const
{
    size_t loading_subtile_count = 0;
    for (const auto& subtile : subtiles)
    {
        if (subtile._load_status == Subtile::LoadStatus::LOADING) loading_subtile_count += 1;
    }
    assert(_subtiles_left_to_load == loading_subtile_count);

    assert(_load_status != LoadStatus::RENDERABLE || _was_made_renderable_once);
    assert(_load_status != LoadStatus::UNLOADED || !_was_made_renderable_once);
}
#endif

ThreeDTile ThreeDTile::from_descriptor(
    hrz::three_d_tiles::ThreeDTilesTilesetDescriptor::Tile&& descriptor_tile,
    uint32_t index)
{
    ThreeDTile tile{};

    tile.depth = descriptor_tile.depth;
    tile.index = index;
    tile.parent_index = descriptor_tile.parent_index;
    tile.child_count = descriptor_tile.child_count;
    tile.first_child_link_index = descriptor_tile.first_child_link_index;
    tile.refinement_type = descriptor_tile.refinement_type;

    tile.geometric_error = descriptor_tile.geometric_error;

    tile.uri = std::move(descriptor_tile.uri);
    tile.range = descriptor_tile.range;

    tile._load_status = ThreeDTile::LoadStatus::UNLOADED;
    tile.render_children_in.reset();
    tile.render_self_in.reset();
    tile.culled_in.reset();
    tile._active_child_count = 0;
    tile._renderable_child_count = 0;
    std::ranges::fill(tile.rendered_child_count_per_view, 0);
    tile.should_render_children_in.reset();
    tile.delete_if_unused = true;

    tile.base_transform = descriptor_tile.transform;
    tile.normal_transform = lm::mat3(hrz::compute_normal_transform_matrix(tile.base_transform));

    tile.bounding_volume = descriptor_tile.bounding_volume;
    if (descriptor_tile.viewer_bounding_volume.has_value())
    {
        tile.viewer_bounding_volume = descriptor_tile.viewer_bounding_volume.value();
    }
    if (descriptor_tile.content_bounding_volume.has_value())
    {
        tile.content_bounding_volume = descriptor_tile.content_bounding_volume.value();
        tile.horizon_occlusion_point = hrz::three_d_tiles::compute_horizon_occlusion_point(
            tile.content_bounding_volume.value());
    }
    else
    {
        tile.horizon_occlusion_point =
            hrz::three_d_tiles::compute_horizon_occlusion_point(tile.bounding_volume);
    }

    tile.has_unsupported_subtiles = false;
    tile._subtiles_left_to_load = 0;

    return tile;
}

hrz::RenderRequest TilesetConfig::update_visibility_constraints(
    std::span<const hrz::RenderViewInfo> views_info)
{
    hrz::RenderRequest render_request;

    const auto new_result =
        hrz::layers::are_visibility_constraints_satisfied(views_info, visibility_constraints);

    if (new_result != visibility_constraints_result)
    {
        render_request.request_visual_render();
    }

    visibility_constraints_result = new_result;

    return render_request;
}

double Tileset::get_lower_geometric_error_in_parent_tiles(uint32_t tile_index) const
{
    const auto* tile = &tiles.at(tile_index);
    const double tile_geometric_error = tile->geometric_error;

    while (tile_index != root_tile_index)
    {
        const auto* parent_tile = &tiles.at(tile->parent_index);
        if (parent_tile->geometric_error <= tile_geometric_error)
        {
            return parent_tile->geometric_error;
        }

        tile_index = tile->parent_index;
        tile = parent_tile;
    }

    if (tile_index == root_tile_index)
    {
        return std::min(geometric_error, tile_geometric_error);
    }

    return tile_geometric_error;
}

hrz::RenderRequest Tileset::make_tile_renderable(uint32_t tile_index)
{
    set_tile_load_status(tiles.at(tile_index), ThreeDTile::LoadStatus::RENDERABLE);
    return hrz::RenderRequest::visual();
}

#if HRZ_DEBUG
static bool is_allowed_load_status_transition(
    ThreeDTile::LoadStatus from,
    ThreeDTile::LoadStatus to)
{
    using Status = ThreeDTile::LoadStatus;

    switch (to)
    {
        case Status::UNLOADED: return true;
        case Status::LOAD_WHEN_INSIDE_VIEWER_VOLUME: return from == Status::UNLOADED;
        case Status::LOADING_TILE:
            return from == Status::UNLOADED || from == Status::LOAD_WHEN_INSIDE_VIEWER_VOLUME;
        case Status::LOADING_CONTENT:
            return from == Status::UNLOADED || from == Status::LOADING_TILE
                || from == Status::LOADED || from == Status::RENDERABLE || from == Status::ERROR;
        case Status::LOADED: return from == Status::UNLOADED || from == Status::LOADING_CONTENT;
        case Status::RENDERABLE: return from == Status::LOADED;
        case Status::ERROR: return from == Status::LOADING_TILE || from == Status::LOADING_CONTENT;
    }

    return false;
}
#endif

void Tileset::set_tile_load_status(ThreeDTile& tile, ThreeDTile::LoadStatus status)
{
#if HRZ_DEBUG
    assert(is_allowed_load_status_transition(tile._load_status, status));
#endif

    const bool was_active = tile.counts_as_active_child();
    const bool was_renderable = tile.counts_as_renderable_child();

    tile._load_status = status;

    if (status == ThreeDTile::LoadStatus::RENDERABLE)
    {
        tile._was_made_renderable_once = true;
    }
    else if (status == ThreeDTile::LoadStatus::UNLOADED)
    {
        tile._was_made_renderable_once = false;
    }

    if (tile.index != root_tile_index)
    {
        auto& parent_tile = tiles.at(tile.parent_index);
        parent_tile._active_child_count += (int)tile.counts_as_active_child() - (int)was_active;
        parent_tile._renderable_child_count +=
            (int)tile.counts_as_renderable_child() - (int)was_renderable;

#if HRZ_DEBUG
        check_tile_invariants(parent_tile);
#endif
    }
}

#if HRZ_DEBUG
void Tileset::check_tile_invariants(const ThreeDTile& tile) const
{
    tile.check_invariants();

    int active_count = 0;
    int renderable_count = 0;

    for (int i = 0; i < tile.child_count; ++i)
    {
        const auto& child_tile = tiles.at(child_links.at(tile.first_child_link_index + i));
        assert(child_tile.parent_index == tile.index);

        active_count += child_tile.counts_as_active_child() ? 1 : 0;
        renderable_count += child_tile.counts_as_renderable_child() ? 1 : 0;
    }

    assert(tile.active_child_count() == active_count);
    assert(tile.renderable_child_count() == renderable_count);
}
#endif

void Tileset::start_decoding_descriptor(
    const hrz::blobs::BlobHandle& descriptor_blob,
    hrz::JobScheduler* js)
{
    hrz_jobs::EncodedThreeDTilesTileset params;
    params.raw_json = descriptor_blob;
    params.transform = transform;
    params.root_depth = root_depth;
    decode_descriptor_ticket = hrz_jobs::add_job_decode_three_d_tiles_tileset(
        js, std::move(params),
        {hrz::monitoring::systems::ThreeDTilesLayers, config->global_layer_id});

    status = Status::DECODING_DESCRIPTOR;
}

bool Tileset::apply_descriptor(hrz::three_d_tiles::ThreeDTilesTilesetDescriptor&& descriptor)
{
    const bool has_tiles = !descriptor.tiles.empty();

    for (size_t i = 0; i < descriptor.tiles.size(); ++i)
    {
        tiles.push_back(ThreeDTile::from_descriptor(std::move(descriptor.tiles.at(i)), i));
    }

    root_tile_index = descriptor.root_tile_index;
    child_links = std::move(descriptor.child_links);
    allow_gltf_content = descriptor.allow_gltf_content;

    // If the tileset is external, some properties take their value
    // from its parent tileset. They are set in load_external_tileset.
    if (is_root())
    {
        geometric_error = descriptor.geometric_error;
        gltf_up_axis = descriptor.gltf_up_axis;
    }

    if (!has_tiles)
    {
        status = Status::ERROR;
        return false;
    }

    render = is_root();
    status = Status::LOADED;
    return true;
}

} // namespace hrz::three_d_tiles
