// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/blob_allocator.h"
#include "hrz/common/metrics.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/common/three_d_tiles.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/base_url.h"
#include "hrz/core/loading_priorities.h"
#include "hrz/core/model/model.h"
#include "hrz/core/render/context.h"
#include "hrz/core/scene.h"
#include "hrz/core/three_d_tiles/model_cache.h"
#include "hrz/core/three_d_tiles/system.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/core/vector/data_loader/data_loader.h"
#include "hrz/core/visibility_constraints.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/maths.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/meta.h"

#include <cassert>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace hrz::three_d_tiles
{

namespace
{

bool load_3dtiles_content(
    const ContentWorkContext& ctx,
    const Tileset* tileset,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* subtile_ident,
    const hrz::blobs::BlobHandle& tile_data_blob);

bool load_b3dm(
    const ContentWorkContext& ctx,
    const Tileset* tileset,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* ident,
    const hrz::blobs::BlobHandle& tile_data_blob)
{
    auto subtile =
        ThreeDTile::B3dmContent::load_content(ctx, *tileset, tile, tile_data_blob, *ident);
    if (!subtile.has_value()) return false;

    tile.add_loaded_subtile(*tileset, std::move(subtile.value()), ident);

    return true;
}

bool load_i3dm(
    const ContentWorkContext& ctx,
    const Tileset* tileset,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* ident,
    const hrz::blobs::BlobHandle& tile_data_blob)
{
    auto subtile = ThreeDTile::I3dmContent::load_content(
        ctx, *tileset, tile, tile_data_blob, *ident, *ctx.model_cache);
    if (!subtile.has_value()) return false;

    tile.add_loaded_subtile(*tileset, std::move(subtile.value()), ident);

    return true;
}

bool load_pnts(
    const ContentWorkContext& ctx,
    const Tileset* tileset,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* ident,
    const hrz::blobs::BlobHandle& tile_data_blob)
{
    auto subtile =
        ThreeDTile::PntsContent::load_content(ctx, tileset->config, tile_data_blob, *ident);
    if (!subtile.has_value()) return false;

    tile.add_loaded_subtile(*tileset, std::move(subtile.value()), ident);

    return true;
}

bool load_gltf_if_allowed(
    const ContentWorkContext& ctx,
    const Tileset* tileset,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* ident,
    const hrz::blobs::BlobHandle& tile_data_blob)
{
    if (!tileset->allow_gltf_content)
    {
        return false;
    }

    auto subtile =
        ThreeDTile::B3dmContent::load_gltf_content(ctx, *tileset, tile, tile_data_blob, *ident);
    if (!subtile.has_value()) return false;

    tile.add_loaded_subtile(*tileset, std::move(subtile.value()), ident);

    return true;
}

bool load_cmpt(
    const ContentWorkContext& ctx,
    const Tileset* tileset,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* subtile_ident,
    const hrz::blobs::BlobHandle& tile_data_blob)
{
    const auto tile_data = tile_data_blob.get_data();
    const size_t tile_data_size = tile_data.size();

    const auto magic = tile_data.subspan(0, 4);

    if (std::strncmp((const char*)magic.data(), "cmpt", 4) != 0) return false;

    const auto header_size = 16;

    if (tile_data_size < header_size)
    {
        HRZ_LOG_ERROR(
            "Not enough data received for cmpt tile \"{}\": {}", tile.uri, tile_data_size);
        return false;
    }

    const auto header_data = tile_data.subspan(0, header_size);

    auto format_version = hrz::read_le_unsafe<uint32_t>(header_data, 4);
    if (format_version != 1)
    {
        HRZ_LOG_ERROR("Unsupported cmpt version in tile \"{}\": {}", tile.uri, format_version);
        return false;
    }

    const auto declared_size = hrz::read_le_unsafe<uint32_t>(header_data, 8);
    if (declared_size != tile_data_size)
    {
        HRZ_LOG_ERROR(
            "Wrong cmpt size in tile \"{}\": Expected {}, received {}", tile.uri, declared_size,
            tile_data_size);
        return false;
    }

    const auto subtile_count = hrz::read_le_unsafe<uint32_t>(header_data, 12);

    if (subtile_count > 0)
    {
        size_t current_subtile_offset = header_size;
        const size_t subtile_header_size = 12;

        for (uint32_t i = 0; i < subtile_count; ++i)
        {
            const auto subtile_data = tile_data.subspan(current_subtile_offset);
            if (subtile_data.size_bytes() < subtile_header_size)
            {
                HRZ_LOG_ERROR(
                    "Not enough header data for subtile {} of composite tile \"{}\"", i, tile.uri);
                break;
            }

            const uint32_t subtile_size = hrz::read_le_unsafe<uint32_t>(subtile_data, 8);
            if (subtile_data.size_bytes() < subtile_size)
            {
                HRZ_LOG_ERROR(
                    "Not enough total data for subtile {} of composite tile \"{}\"", i, tile.uri);
                break;
            }

            const auto subtile_data_blob = hrz::blobs::make_sub_blob(
                hrz::unsafe("Offset and size are checked above"), ctx.ba, tile_data_blob,
                current_subtile_offset, subtile_size);

            // Composite tiles can only contain other 3D Tiles formats, no raw glTF or external
            // tilesets.
            if (!load_3dtiles_content(ctx, tileset, tile, subtile_ident, subtile_data_blob))
            {
                tile.has_unsupported_subtiles = true;
            }

            current_subtile_offset += subtile_size;
        }
    }

    return true;
}

bool load_3dtiles_content(
    const ContentWorkContext& ctx,
    const Tileset* tileset,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* subtile_ident,
    const hrz::blobs::BlobHandle& tile_data_blob)
{
    if (load_b3dm(ctx, tileset, tile, subtile_ident, tile_data_blob))
    {
        return true;
    }
    else if (load_i3dm(ctx, tileset, tile, subtile_ident, tile_data_blob))
    {
        return true;
    }
    else if (load_pnts(ctx, tileset, tile, subtile_ident, tile_data_blob))
    {
        return true;
    }
    else
    {
        return load_cmpt(ctx, tileset, tile, subtile_ident, tile_data_blob);
    }
}

bool load_content(
    const ContentWorkContext& ctx,
    const Tileset* tileset,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* subtile_ident,
    const hrz::blobs::BlobHandle& tile_data_blob)
{
    if (load_gltf_if_allowed(ctx, tileset, tile, subtile_ident, tile_data_blob))
    {
        return true;
    }
    else
    {
        return load_3dtiles_content(ctx, tileset, tile, subtile_ident, tile_data_blob);
    }
}

uint32_t compute_request_priority(
    const ThreeDTile& tile,
    const TilesetConfig& config,
    std::span<const hrz::RenderViewInfo> views_info,
    hrz::SceneViewBitset visiting_in)
{
    double distance_to_view = DBL_MAX;
    for (const auto& view_info : views_info)
    {
        if (!visiting_in.is_set((int)view_info.view)) continue;

        const double dist = distance(tile.bounding_volume, view_info.cam_view_info.cam.pos);
        if (dist < distance_to_view)
        {
            distance_to_view = dist;
        }
    }

    return hrz::combine_loading_priorities(
        config.loading_priority,
        std::numeric_limits<uint16_t>::max() - hrz::clamp_cast<double, uint16_t>(distance_to_view));
}

} // anonymous namespace

bool ThreeDTilesSystem::load_external_tileset(
    Tileset* tileset,
    uint32_t tile_index,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* ident,
    const hrz::blobs::BlobHandle& tile_data_blob,
    const WorkContext& ctx)
{
    const auto handle = _tilesets.alloc();
    auto external_tileset = _tilesets.get_object(handle);

    external_tileset->config = tileset->config;
    external_tileset->root_depth = tile.depth;

    external_tileset->transform = tile.base_transform;

    // This value usually determines when the root tile is displayed.
    // However, when a tileset is an external tileset, the decision to
    // display its root tile is made by its parent tileset.
    // Its geometric error is set to what will in practice decides its
    // visibility. This value is useful for the handling of empty tiles.
    external_tileset->geometric_error =
        tileset->get_lower_geometric_error_in_parent_tiles(tile_index);

    // Only the value of the gltfUpAxis property of the root tileset is
    // taken into account.
    external_tileset->gltf_up_axis = tileset->gltf_up_axis;

    external_tileset->base_url = tileset->base_url.derive_base(tile.uri);

    external_tileset->vector_data_attribute_request_id = handle;

    external_tileset->start_decoding_descriptor(tile_data_blob, ctx.js);

    external_tileset->requests_count_metric = hrz::metrics::MetricDesc(
        "3D Tiles (requests tally)", false,
        {{"layer", std::to_string(external_tileset->config->layer_handle)},
         {"url", external_tileset->base_url.base().c_str()}});
    hrz::metrics::increment_counter(&tileset->requests_count_metric);

    _loading_tilesets.insert(handle);

    ThreeDTile::Subtile subtile;
    ThreeDTile::TilesetContent content;
    content.tileset_handle = handle;
    subtile.content = content;

    tile.add_loaded_subtile(*tileset, std::move(subtile), ident);
    assert(
        tile.subtiles.size()
        == 1); // We should only have 1 subtile if this tile is an external dataset

    return true;
}

bool ThreeDTilesSystem::decode_tile_data(
    Tileset* tileset,
    uint32_t tile_index,
    ThreeDTile& tile,
    ThreeDTile::SubtileIdentification* subtile_ident,
    const hrz::blobs::BlobHandle& tile_data_blob,
    std::string_view tile_mime_type,
    const WorkContext& ctx)
{
    HRZ_SCOPED_SAMPLE("decode tile data");

    const auto tile_data_size = tile_data_blob.data_size();
    if (tile_data_size < 4)
    {
        HRZ_LOG_ERROR("Not enough data in tile \"{}\"", tile.uri);
        return false;
    }

    const auto tile_data = tile_data_blob.get_data();

    if (tile_mime_type == "application/json")
    {
        if (load_external_tileset(tileset, tile_index, tile, subtile_ident, tile_data_blob, ctx))
        {
            return true;
        }
    }
    else if (tile_mime_type == "model/gltf-binary" || tile_mime_type == "model/gltf+json")
    {
        if (load_gltf_if_allowed(_content_work_ctx, tileset, tile, subtile_ident, tile_data_blob))
        {
            return true;
        }
    }
    else if (tile_mime_type == "application/octet-stream")
    {
        if (load_content(_content_work_ctx, tileset, tile, subtile_ident, tile_data_blob))
        {
            return true;
        }
    }

    if (tile.uri.ends_with(".json"))
    {
        // This is probably an external tileset, sent with the wrong content-type header.
        if (load_external_tileset(tileset, tile_index, tile, subtile_ident, tile_data_blob, ctx))
        {
            return true;
        }
    }
    else if (tile.uri.ends_with(".b3dm"))
    {
        // This is probably a b3dm, sent with the wrong content-type header.
        if (load_b3dm(_content_work_ctx, tileset, tile, subtile_ident, tile_data_blob))
        {
            return true;
        }
    }
    else if (tile.uri.ends_with(".i3dm"))
    {
        // This is probably an i3dm, sent with the wrong content-type header.
        if (load_i3dm(_content_work_ctx, tileset, tile, subtile_ident, tile_data_blob))
        {
            return true;
        }
    }
    else if (tile.uri.ends_with(".pnts"))
    {
        // This is probably a pnts, sent with the wrong content-type header.
        if (load_pnts(_content_work_ctx, tileset, tile, subtile_ident, tile_data_blob))
        {
            return true;
        }
    }
    else if (tile.uri.ends_with(".cmpt"))
    {
        // This is probably a cmpt, sent with the wrong content-type header.
        if (load_cmpt(_content_work_ctx, tileset, tile, subtile_ident, tile_data_blob))
        {
            return true;
        }
    }
    else if (tile.uri.ends_with(".glb") || tile.uri.ends_with(".gltf"))
    {
        // This is probably a gltf, sent with the wrong content-type header.
        if (load_gltf_if_allowed(_content_work_ctx, tileset, tile, subtile_ident, tile_data_blob))
        {
            return true;
        }
    }

    // Still no luck, both the content-type header and the extension are wrong.
    // Let's try all supported content formats.
    if (load_content(_content_work_ctx, tileset, tile, subtile_ident, tile_data_blob))
    {
        return true;
    }

    HRZ_LOG_ERROR("Unsupported format for tile \"{}\"", tile.uri);
    tile.has_unsupported_subtiles = true;
    return false;
}

void ThreeDTilesSystem::start_loading_tile_data(
    Tileset* tileset,
    ThreeDTile* tile,
    const TilesetWorkContext& ctx)
{
    if (!tile->range.has_value())
    {
        tile->load_data_ticket = hrz::assets_loader::begin(
            ctx.al, tileset->base_url.derive(tile->uri), tileset->config->headers,
            tileset->config->get_request_queue(),
            compute_request_priority(*tile, *tileset->config, ctx.views_info, ctx.visiting_in),
            {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});
    }
    else
    {
        tile->load_data_ticket = hrz::assets_loader::begin(
            ctx.al, tileset->base_url.derive(tile->uri), tile->range.value().offset,
            tile->range.value().length, tileset->config->headers,
            tileset->config->get_request_queue(),
            compute_request_priority(*tile, *tileset->config, ctx.views_info, ctx.visiting_in),
            {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});
    }
    hrz::metrics::increment_counter(&tileset->requests_count_metric);
    tileset->set_tile_load_status(*tile, ThreeDTile::LoadStatus::LOADING_TILE);
}

void ThreeDTilesSystem::load_tile(
    Tileset* tileset,
    uint32_t tile_index,
    ThreeDTile& tile,
    const TilesetWorkContext& ctx)
{
    if (tile.uri.size() > 0)
    {
        if (tile.viewer_bounding_volume.has_value())
        {
            tileset->set_tile_load_status(
                tile, ThreeDTile::LoadStatus::LOAD_WHEN_INSIDE_VIEWER_VOLUME);
        }
        else
        {
            start_loading_tile_data(tileset, &tile, ctx);
        }
    }
    else
    {
        // The tile is empty.
        // The 3D Tiles specification says that the tile must be considered loaded.
        // If the parent tile has content, and the [grand-]children also have content,
        // it could result in a transient empty space on the planet.
        // However Cesium themselves use a different strategy and only consider an
        // empty tile as loaded if its children are displayed (recursively), so that
        // content is always shown.
        // Some 3D Tiles producers rely on that behaviour for smooth loading and
        // display of their tilesets.
        // Here empty tiles with children are not set to loaded, but to be loading
        // their content. Then the loading code sets them to loaded once the children
        // are loaded. This recursively forces empty tiles that are between non-empty
        // tiles to wait until content has been loaded, and prevents the apparition of
        // holes.
        // However some of the [grand-]children may not be close enough, or in the
        // camera frustum, and will not load. The loading code sets the empty tile to
        // the loaded state in that case, to avoid blocking a whole sub-tree.
        // See
        //     - https://github.com/CesiumGS/3d-tiles/issues/609
        //     - https://github.com/CesiumGS/cesium/issues/9356
        //     - https://github.com/CesiumGS/cesium-native/pull/488
        tileset->set_tile_load_status(
            tile,
            tile.child_count > 0 ? ThreeDTile::LoadStatus::LOADING_CONTENT
                                 : ThreeDTile::LoadStatus::LOADED);

        tile.is_empty_structural = tile.load_status() == ThreeDTile::LoadStatus::LOADING_CONTENT
            && tile.geometric_error
                > tileset->get_lower_geometric_error_in_parent_tiles(tile_index);
    }

    tile.delete_if_unused = false;
    tileset->active_tiles.insert(tile_index);
}

void ThreeDTilesSystem::cancel_downloads(TilesetH handle, hrz::AssetsLoader* al)
{
    auto tileset = _tilesets.get_object(handle);

    for (auto& tile : tileset->tiles)
    {
        for (auto& subtile : tile.subtiles)
        {
            if (subtile.has_external_tileset())
            {
                const auto& content = std::get<ThreeDTile::TilesetContent>(subtile.content);
                if (content.tileset_handle != 0)
                {
                    cancel_downloads(content.tileset_handle, al);
                }
            }
        }
    }

    const bool tileset_is_loaded = _loaded_tilesets.find(handle) != _loaded_tilesets.end();
    if (tileset_is_loaded)
    {
        for (auto& tile_index : tileset->active_tiles)
        {
            auto& tile = tileset->tiles.at(tile_index);

            if (tile.load_status() == ThreeDTile::LoadStatus::LOADING_TILE)
            {
                hrz::assets_loader::end(al, tile.load_data_ticket);
                tileset->set_tile_load_status(tile, ThreeDTile::LoadStatus::UNLOADED);
            }

            for (auto& subtile : tile.subtiles)
            {
                cancel_attribute_request(subtile);
            }
        }
    }
}

bool ThreeDTilesSystem::reload_subtiles_content(TilesetH handle, bool allow_external_tilesets)
{
    auto tileset = get_tileset(handle, allow_external_tilesets);
    if (tileset == nullptr) return false;

    bool is_reloading = false;

    for (size_t tile_index = 0; tile_index < tileset->tiles.size(); ++tile_index)
    {
        auto& tile = tileset->tiles[tile_index];
        bool has_subtile_to_load = false;

        for (auto& subtile : tile.subtiles)
        {
            std::visit(
                hrz::overload{
                    [this, &subtile, &tile,
                     &has_subtile_to_load](ThreeDTile::TilesetContent& content)
                    {
                        if (reload_subtiles_content(content.tileset_handle, true))
                        {
                            if (subtile.load_status() != ThreeDTile::Subtile::LoadStatus::LOADING)
                            {
                                tile.set_subtile_load_status(
                                    subtile, ThreeDTile::Subtile::LoadStatus::LOADING);
                            }
                            has_subtile_to_load = true;
                        }
                    },
                    [&subtile, &tile, &has_subtile_to_load](ThreeDTile::B3dmContent&)
                    {
                        if (subtile.load_status() != ThreeDTile::Subtile::LoadStatus::LOADING)
                        {
                            tile.set_subtile_load_status(
                                subtile, ThreeDTile::Subtile::LoadStatus::LOADING);
                        }
                        has_subtile_to_load = true;
                    },
                    [](ThreeDTile::I3dmContent&)
                    {
                        // We do nothing, because reloading happens for materials,
                        // but i3dm and pnts don't use dynamic materials.
                    },
                    [](ThreeDTile::PntsContent&)
                    {
                        // We do nothing, because reloading happens for materials,
                        // but i3dm and pnts don't use dynamic materials.
                    },
                    [](std::monostate&)
                    {
                        // We do nothing, because reloading happens for materials,
                        // but i3dm and pnts don't use dynamic materials.
                    }
                },
                subtile.content);
        }

        if (has_subtile_to_load
            && (tile.load_status() == ThreeDTile::LoadStatus::LOADED
                || tile.load_status() == ThreeDTile::LoadStatus::RENDERABLE
                || tile.load_status() == ThreeDTile::LoadStatus::ERROR))
        {
            // If a tile is in error, but reached this point, it means it has subtiles,
            // so its load error is beyond loading the tile itself, and we can try
            // reloading the subtiles.
            tileset->set_tile_load_status(tile, ThreeDTile::LoadStatus::LOADING_CONTENT);
            is_reloading = true;
        }
    }

    return is_reloading;
}

void ThreeDTilesSystem::unload_subtile(
    ThreeDTile::Subtile& subtile,
    hrz::AssetsLoader* al,
    hrz::JobScheduler* js,
    hrz::BlobAllocator* ba)
{
    std::visit(
        hrz::overload{
            [this](ContentSubtile auto& content)
            { content.unload(_content_work_ctx, _removed_resources); },
            [this, al, js, ba](ThreeDTile::TilesetContent& content)
            {
                if (content.tileset_handle != 0)
                {
                    unload_tileset(content.tileset_handle, al, js, ba);
                }
            },
            [](std::monostate&) {},
        },
        subtile.content);

    release_attribute_request(subtile);
    subtile.reset_styling_status(js);
    hrz_jobs::cancel_job(js, subtile.decode_batch_table_ticket);
}

void ThreeDTilesSystem::unload_tile(
    Tileset* tileset,
    ThreeDTile& tile,
    hrz::AssetsLoader* al,
    hrz::JobScheduler* js,
    hrz::BlobAllocator* ba)
{
    HRZ_SCOPED_SAMPLE_A("unload tile");

    for (auto& subtile : tile.subtiles)
    {
        unload_subtile(subtile, al, js, ba);
    }
    tile.clear_subtiles();

    hrz::assets_loader::end(al, tile.load_data_ticket);

    tileset->set_tile_load_status(tile, ThreeDTile::LoadStatus::UNLOADED);
}

void ThreeDTilesSystem::unload_tileset(
    TilesetH handle,
    hrz::AssetsLoader* al,
    hrz::JobScheduler* js,
    hrz::BlobAllocator* ba)
{
    HRZ_SCOPED_SAMPLE("unload tileset");

    _loading_tilesets.erase(handle);
    _loaded_tilesets.erase(handle);
    _root_tilesets.erase(handle);

    auto tileset = _tilesets.get_object(handle);
    if (!tileset) return;

    hrz::assets_loader::end(al, tileset->load_descriptor_ticket);
    hrz_jobs::cancel_job(js, tileset->decode_descriptor_ticket);

    for (auto& tile : tileset->tiles)
    {
        unload_tile(tileset, tile, al, js, ba);
    }

    if (tileset->is_root())
    {
        _vector_data_channel.send(
            hrz::vector_data::messages::ReleaseLayerLoader{
                tileset->config->vector_data_layer_request_id
            });
    }

    _tilesets.release(handle);
}

void ThreeDTilesSystem::work_loading_tilesets(
    hrz::AssetsLoader* al,
    hrz::JobScheduler* js,
    hrz::BlobAllocator* ba)
{
    HRZ_SCOPED_SAMPLE("work loading tilesets");

    for (auto it = _loading_tilesets.begin(); it != _loading_tilesets.end();)
    {
        const TilesetH handle = *it;
        auto tileset = _tilesets.get_object(handle);

        if (!tileset->config->is_visible
            || !tileset->config->visibility_constraints_result.satisfied_in)
        {
            ++it;
            continue;
        }

        bool erase = false;
        if (tileset->status == Tileset::Status::LOADING_DESCRIPTOR)
        {
            if (hrz::assets_loader::is_finished(al, tileset->load_descriptor_ticket))
            {
                if (hrz::assets_loader::get_status(al, tileset->load_descriptor_ticket)
                    == hrz::assets_loader::RequestStatus::Loaded)
                {
                    const auto descriptor_blob =
                        hrz::assets_loader::get_blob(al, ba, tileset->load_descriptor_ticket);
                    tileset->start_decoding_descriptor(std::move(descriptor_blob), js);
                }
                else
                {
                    HRZ_LOG_ERROR(
                        "Error: Could not load 3D Tiles descriptor at \"{}\"", tileset->url);

                    tileset->status = Tileset::Status::ERROR;

                    erase = true;
                }

                hrz::assets_loader::end(al, tileset->load_descriptor_ticket);
            }
        }
        else if (tileset->status == Tileset::Status::DECODING_DESCRIPTOR)
        {
            if (hrz_jobs::is_job_finished(js, tileset->decode_descriptor_ticket))
            {
                if (hrz_jobs::get_job_status(js, tileset->decode_descriptor_ticket)
                    == hrz::job_scheduler::JobStatus::Finished_Success)
                {
                    auto response =
                        hrz_jobs::get_job_response(js, tileset->decode_descriptor_ticket);

                    if (tileset->apply_descriptor(std::move(response)))
                    {
                        _loaded_tilesets.insert(handle);
                    }
                    else
                    {
                        HRZ_LOG_ERROR("No tiles in tileset descriptor at \"{}\"", tileset->url);
                    }
                }
                else
                {
                    hrz_jobs::cancel_job(js, tileset->decode_descriptor_ticket);

                    HRZ_LOG_ERROR(
                        "Error while decoding 3D Tiles tileset descriptor at \"{}\"", tileset->url);

                    tileset->status = Tileset::Status::ERROR;
                }

                erase = true;
            }
        }
        else
        {
            assert(false && "Invalid state");
        }

        if (erase)
        {
            _loading_tilesets.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

void ThreeDTilesSystem::work_loading_tile(
    TilesetH tileset_handle,
    Tileset* tileset,
    uint32_t tile_index,
    ThreeDTile* tile,
    const TilesetWorkContext& ctx)
{
    HRZ_SCOPED_SAMPLE_A("work loading tiles");

    if (!hrz::assets_loader::is_valid(ctx.al, tile->load_data_ticket))
    {
        // The data loading job may have been cancelled if the layer visibility has
        // been switched off. If we reach here now, it means that the visibility has
        // been turned back on. In that case, restart the loading job.
        start_loading_tile_data(tileset, tile, ctx);
    }

    if (hrz::assets_loader::is_finished(ctx.al, tile->load_data_ticket))
    {
        if (hrz::assets_loader::get_status(ctx.al, tile->load_data_ticket)
            == hrz::assets_loader::RequestStatus::Loaded)
        {
            const auto tile_mime_type =
                tile->range.has_value() && tile->range->mime_type.has_value()
                ? std::string_view(tile->range->mime_type.value())
                : hrz::assets_loader::get_content_type(ctx.al, tile->load_data_ticket);

            const auto tile_data_blob =
                hrz::assets_loader::get_blob(ctx.al, ctx.ba, tile->load_data_ticket);

            ThreeDTile::SubtileIdentification subtile_ident{
                .object_reference =
                    make_object_reference(_system_picking_id, tileset_handle, tile_index),
                .feature_reference = hrz::picking::FeatureReference(
                    _system_picking_id, tileset->config->layer_handle),
                .batch_id_offset = 0,
            };

            const auto success = decode_tile_data(
                tileset, tile_index, *tile, &subtile_ident, tile_data_blob, tile_mime_type, ctx);

            hrz::assets_loader::end(ctx.al, tile->load_data_ticket);

            if (success)
            {
                tileset->set_tile_load_status(*tile, ThreeDTile::LoadStatus::LOADING_CONTENT);
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Could not decode tile data at \"{}\"", tileset->base_url.derive(tile->uri));

                tileset->set_tile_load_status(*tile, ThreeDTile::LoadStatus::ERROR);
            }
        }
        else
        {
            HRZ_LOG_ERROR(
                "Could not load tile data at \"{}\"", tileset->base_url.derive(tile->uri));

            hrz::assets_loader::end(ctx.al, tile->load_data_ticket);

            tileset->set_tile_load_status(*tile, ThreeDTile::LoadStatus::ERROR);
        }
    }
    else if (ctx.should_update_priorities)
    {
        const uint32_t priority =
            compute_request_priority(*tile, *tileset->config, ctx.views_info, ctx.visiting_in);
        tile->load_data_ticket =
            hrz::assets_loader::reset_priority(ctx.al, tile->load_data_ticket, priority);
    }
}

hrz::RenderRequest ThreeDTilesSystem::work_tile_load_status(
    Tileset* tileset,
    TilesetH tileset_handle,
    uint32_t tile_index,
    ThreeDTile& tile,
    const TilesetWorkContext& ctx)
{
    hrz::RenderRequest render_request;

    if (tile.load_status() == ThreeDTile::LoadStatus::LOAD_WHEN_INSIDE_VIEWER_VOLUME)
    {
        assert(tile.viewer_bounding_volume.has_value());

        for (const auto& view_info : ctx.views_info)
        {
            const auto distance = hrz::three_d_tiles::distance(
                tile.viewer_bounding_volume.value(), view_info.cam_view_info.cam.pos);

            if (distance <= 0)
            {
                // One of the cameras is inside the viewer request
                // volume, the tile's contents can be loaded.
                start_loading_tile_data(tileset, &tile, ctx);
                break;
            }
        }
    }
    else if (tile.load_status() == ThreeDTile::LoadStatus::LOADING_TILE)
    {
        work_loading_tile(tileset_handle, tileset, tile_index, &tile, ctx);
    }
    else if (tile.load_status() == ThreeDTile::LoadStatus::LOADING_CONTENT)
    {
        render_request |= work_loading_tile_content(tileset, tile, ctx);
    }

    return render_request;
}

hrz::RenderRequest ThreeDTilesSystem::work_loading_tile_content(
    Tileset* tileset,
    ThreeDTile& tile,
    const TilesetWorkContext& ctx)
{
    hrz::RenderRequest render_request;

    HRZ_SCOPED_SAMPLE_A("work decoding tiles");

    if (tile.subtiles.empty() && !tile.has_unsupported_subtiles)
    {
        // An empty tile that has been set to the loading content state.
        // This means that it should wait for its [grand-]children to
        // load content before setting itself to loaded, in order to avoid
        // showing holes.
        // If all its children are loaded and renderable, the tile is itself
        // loaded.
        // If the tile has a greater geometric error than its parent (or
        // grand-parents), it means that the tile is purely structural and
        // should not be displayed, so the tile is left in the loading
        // content state all long as the children are not renderable.
        // If the tile has an expected lower geometric error than its parent,
        // either it has active children, meaning that it needs to be refined,
        // or it has no active children, and therefore doesn't need to be
        // refined. (The decision to load children took place between now
        // and the moment when it was put in the loading content state.)
        // If it needs to be refined, it means that the tile is on screen,
        // and the children will soon be ready. To avoid showing the empty
        // tile, it is left in the loading content state.
        // If the tile doesn't need to be refined, it's a regular empty tile
        // that is too far from the camera to show its children, so it's set
        // to loaded, in order to avoid blocking its parent tile from being
        // effectively refined.
        if (tile.renderable_child_count() == tile.child_count
            || (!tile.is_empty_structural && tile.active_child_count() == 0))
        {
            tileset->set_tile_load_status(tile, ThreeDTile::LoadStatus::LOADED);
        }
    }
    else
    {
        for (auto& subtile : tile.subtiles)
        {
            if (subtile.load_status() != ThreeDTile::Subtile::LoadStatus::LOADING) continue;

            render_request |= std::visit(
                hrz::overload{
                    [&](ContentSubtile auto& content)
                    {
                        if (subtile.poll_batch_table(ctx.js)
                            == ThreeDTile::Subtile::BatchTablePollResult::ERROR)
                        {
                            tile.set_subtile_load_status(
                                subtile, ThreeDTile::Subtile::LoadStatus::ERROR);
                            return hrz::RenderRequest{};
                        }

                        const auto status =
                            content.work_loading(_content_work_ctx, tile, subtile, tileset->config);
                        if (status == ThreeDTile::ContentLoadStatusUpdate::LOADED)
                        {
                            tile.set_subtile_load_status(
                                subtile, ThreeDTile::Subtile::LoadStatus::LOADED);
                            return hrz::RenderRequest::visual();
                        }
                        else if (status == ThreeDTile::ContentLoadStatusUpdate::ERROR)
                        {
                            HRZ_LOG_ERROR(
                                "Could not load subtile content at \"{}\"",
                                tileset->base_url.derive(tile.uri));
                            tile.set_subtile_load_status(
                                subtile, ThreeDTile::Subtile::LoadStatus::ERROR);
                        }
                        return hrz::RenderRequest{};
                    },
                    [&](ThreeDTile::TilesetContent& content)
                    {
                        const auto* child_tileset = _tilesets.get_object(content.tileset_handle);
                        const auto root_tile_status = child_tileset->get_root_tile_status();
                        if (root_tile_status == ThreeDTile::LoadStatus::RENDERABLE)
                        {
                            tile.set_subtile_load_status(
                                subtile, ThreeDTile::Subtile::LoadStatus::LOADED);
                        }
                        else if (root_tile_status == ThreeDTile::LoadStatus::ERROR)
                        {
                            tile.set_subtile_load_status(
                                subtile, ThreeDTile::Subtile::LoadStatus::ERROR);
                        }
                        return hrz::RenderRequest{};
                    },
                    [&](std::monostate&) { return hrz::RenderRequest{}; }
                },
                subtile.content);
        }

        if (tile.subtiles_left_to_load() == 0)
        {
            bool has_subtile_error = false;
            for (const auto& subtile : tile.subtiles)
            {
                has_subtile_error |=
                    subtile.load_status() == ThreeDTile::Subtile::LoadStatus::ERROR;
            }
            tileset->set_tile_load_status(
                tile,
                has_subtile_error ? ThreeDTile::LoadStatus::ERROR : ThreeDTile::LoadStatus::LOADED);
        }
    }

    return render_request;
}

} // namespace hrz::three_d_tiles
