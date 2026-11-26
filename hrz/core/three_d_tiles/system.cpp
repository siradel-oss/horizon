// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/three_d_tiles/system.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/metrics.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/common/style/flat_ast.h"
#include "hrz/common/three_d_tiles.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/base_url.h"
#include "hrz/core/clock.h"
#include "hrz/core/job_scheduler.h"
#include "hrz/core/loading_priorities.h"
#include "hrz/core/model/model.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/lighting_settings.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/scene.h"
#include "hrz/core/shadows.h"
#include "hrz/core/sky.h"
#include "hrz/core/three_d_tiles/model_cache.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/core/vector/data_loader/data_loader.h"
#include "hrz/core/visibility_constraints.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/log.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace hrz::three_d_tiles
{

void ThreeDTilesSystem::init(
    uint8_t system_picking_id,
    hrz::vector_data::VectorDataLoaderChannel&& vector_data_channel)
{
    _system_picking_id = system_picking_id;
    _vector_data_channel = std::move(vector_data_channel);
    _request_priority_timer_ms = hrz::clock::CurrentFrameWallTime.ms;
}

void ThreeDTilesSystem::initialize_rendering(hrz::Render* render)
{
    if (!_model_shared_resources)
    {
        _model_shared_resources = hrz::model::create_shared_resources_batched(render);
    }

    if (!_model_shared_resources_instanced)
    {
        _model_shared_resources_instanced = hrz::model::create_shared_resources_instanced(render);
    }
}

TilesetH ThreeDTilesSystem::add_tileset(
    std::string_view url,
    const hrz::HttpHeaders& headers,
    hrz::AttributionHandle additional_attribution,
    bool preserve_query_parameters,
    const lm::dmat4& transform,
    double max_screen_space_error,
    double refinement_hysteresis,
    const std::vector<hrz::three_d_tiles::AttributeConfig>& attributes,
    LayerH layer_handle,
    uint64_t global_layer_id,
    bool is_visible,
    int8_t loading_priority,
    int32_t clip_id,
    uint32_t scene_views_bitset,
    uint32_t color_blend_mode,
    float color_blend_strength,
    const hrz::render::LightingSettings& lighting_settings,
    const hrz_proto::LayerVisibilityConstraintList& visibility_constraints,
    bool draw_under_flat_overlays,
    hrz::AssetsLoader* al)
{
    const auto handle = _tilesets.alloc();
    auto* tileset = _tilesets.get_object(handle);
    _root_tilesets.insert(handle);

    tileset->owned_config = std::make_unique<TilesetConfig>();
    auto* config = tileset->owned_config.get();

    config->is_visible = is_visible;
    config->visibility_constraints_result = hrz::layers::MultiviewVisibilityConstraints();
    config->visibility_constraints = visibility_constraints;
    config->draw_under_flat_overlays = draw_under_flat_overlays;
    config->additional_attribution = additional_attribution;

    config->layer_handle = layer_handle;
    config->global_layer_id = global_layer_id;

    // Avoid dividing by 0 later-on.
    config->max_screen_space_error = std::max(0.01, max_screen_space_error);
    config->screen_space_error_hysteresis =
        TilesetConfig::ScreenSpaceErrorHysteresis::from_refinement_hysteresis(
            refinement_hysteresis);

    config->loading_priority = loading_priority;

    config->inherited_draw_prps.clip_id = clip_id;
    config->inherited_draw_prps.lighting = lighting_settings;
    config->inherited_draw_prps.color = lm::vec4(1.0F);
    config->inherited_draw_prps.apply_feature_color_to_overlay = false;
    config->inherited_draw_prps.overlay_material_enabled = false;
    config->inherited_draw_prps.overlay_material_opacity = 1.0F;
    config->inherited_draw_prps.transform = lm::dmat4::identity();
    config->inherited_draw_prps.feature_color_blend_mode = color_blend_mode;
    config->inherited_draw_prps.feature_color_blend_strength = color_blend_strength;

    config->attributes = attributes;
    config->has_vector_data_layer_attributes = std::ranges::any_of(
        config->attributes, [](const hrz::three_d_tiles::AttributeConfig& attribute)
        { return attribute.has_vector_data_layer_source(); });
    config->vector_data_layer_id = 0;
    config->vector_data_layer_request_id = VectorDataRequestId::for_layer(handle);
    config->vector_data_layer_status = TilesetConfig::VectorDataLayerStatus::UNREQUESTED;
    config->renew_vector_data_layer_request = true;
    config->style_ast = std::make_shared<hrz::style::FlatAst>();
    config->has_style = false;
    config->headers = headers;

    config->scene_views_bitset = scene_views_bitset;

    tileset->config = config;

    tileset->transform = transform;

    // Nothing will be displayed if no value is given in the descriptor.
    tileset->geometric_error = 0;

    tileset->load_descriptor_ticket = hrz::assets_loader::begin(
        al, url, headers, tileset->config->get_request_queue(),
        hrz::combine_loading_priorities(
            tileset->config->loading_priority, std::numeric_limits<uint16_t>::max()),
        {hrz::monitoring::systems::ThreeDTilesLayers, global_layer_id});
    tileset->url = url;
    tileset->base_url = {url, preserve_query_parameters};
    tileset->vector_data_attribute_request_id = handle;
    tileset->status = Tileset::Status::LOADING_DESCRIPTOR;

    tileset->requests_count_metric = hrz::metrics::MetricDesc(
        "3D Tiles (requests tally)", false,
        {{"layer", std::to_string(layer_handle)}, {"url", tileset->base_url.base().c_str()}});
    hrz::metrics::increment_counter(&tileset->requests_count_metric);

    _loading_tilesets.insert(handle);

    return handle;
}

Tileset* ThreeDTilesSystem::get_tileset(TilesetH handle, bool allow_external_tilesets)
{
    auto tileset = _tilesets.get_object(handle);

    if (tileset == nullptr)
    {
        HRZ_LOG_WARNING("Unknown tileset handle: {}", handle);
        return nullptr;
    }

    if (!tileset->is_root() && !allow_external_tilesets)
    {
        HRZ_LOG_WARNING("Cannot configure external tileset ({}) directly", handle);
        return nullptr;
    }

    return tileset;
}

void ThreeDTilesSystem::set_visibility(TilesetH handle, bool is_visible, hrz::AssetsLoader* al)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    tileset->get_mutable_config()->is_visible = is_visible;

    if (!is_visible)
    {
        cancel_downloads(handle, al);
    }
}

void ThreeDTilesSystem::set_visibility_constraints(
    TilesetH handle,
    const hrz_proto::LayerVisibilityConstraintList& visibility_constraints)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    tileset->get_mutable_config()->visibility_constraints = visibility_constraints;
}

bool ThreeDTilesSystem::is_tileset_visible(TilesetH handle)
{
    auto tileset = _tilesets.get_object(handle);

    if (tileset == nullptr)
    {
        HRZ_LOG_WARNING("Unknown tileset handle: {}", handle);
        return false;
    }

    return tileset->config->is_visible
        && tileset->config->visibility_constraints_result.satisfied_in;
}

void ThreeDTilesSystem::set_max_screen_space_error(TilesetH handle, double max_screen_space_error)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    // Avoid dividing by 0 later-on.
    tileset->get_mutable_config()->max_screen_space_error = std::max(0.01, max_screen_space_error);
}

void ThreeDTilesSystem::set_refinement_hysteresis(TilesetH handle, double refinement_hysteresis)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    tileset->get_mutable_config()->screen_space_error_hysteresis =
        TilesetConfig::ScreenSpaceErrorHysteresis::from_refinement_hysteresis(
            refinement_hysteresis);
}

void ThreeDTilesSystem::set_loading_priority(TilesetH handle, int8_t loading_priority)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    tileset->get_mutable_config()->loading_priority = loading_priority;
}

void ThreeDTilesSystem::set_scene_views_bitset(TilesetH handle, uint32_t bitset)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return;

    tileset->get_mutable_config()->scene_views_bitset = bitset;
}

bool ThreeDTilesSystem::set_http_headers(TilesetH handle, const hrz::HttpHeaders& headers)
{
    auto tileset = get_tileset(handle);
    if (tileset == nullptr) return false;

    auto* config = tileset->get_mutable_config();

    const auto old = std::exchange(config->headers, headers);
    return old.hash_content() != config->headers.hash_content();
}

void ThreeDTilesSystem::mark_tileset_to_unload(TilesetH handle)
{
    _loading_tilesets.erase(handle);
    _loaded_tilesets.erase(handle);
    _removed_tilesets.insert(handle);
}

hrz::RenderRequest ThreeDTilesSystem::work(
    hrz::AssetsLoader* al,
    hrz::JobScheduler* js,
    hrz::BlobAllocator* ba,
    hrz::ImageDecoder* imgdec,
    hrz::AttributionRegistry* attributions,
    std::span<const hrz::RenderViewInfo> views_info)
{
    HRZ_SCOPED_SAMPLE("3D tiles system work");

    hrz::RenderRequest render_request;

    for (const auto handle : _removed_tilesets)
    {
        unload_tileset(handle, al, js, ba);
        render_request.request_visual_render();
    }
    _removed_tilesets.clear();

    render_request |= update_visibility_constraints(views_info);

    work_loading_tilesets(al, js, ba);

    const WorkContext ctx{
        .al = al,
        .js = js,
        .ba = ba,
        .imgdec = imgdec,
        .attributions = attributions,
        .views_info = views_info
    };

    const ContentWorkContext content_work_ctx{
        .al = al,
        .js = js,
        .ba = ba,
        .imgdec = imgdec,
        .attributions = attributions,
        .model_cache = &_model_cache,
    };
    _content_work_ctx = content_work_ctx;

    _model_cache.work(al, js, ba, imgdec, attributions, _removed_resources);
    work_attribute_messages(ctx);

    render_request |= work_loaded_tilesets(ctx);

    return render_request;
}

bool ThreeDTilesSystem::is_working()
{
    for (auto& handle : _loading_tilesets)
    {
        const auto& tileset = _tilesets.get_object(handle);
        if (tileset->config->is_visible
            && tileset->config->visibility_constraints_result.satisfied_in)
            return true;
    }

    for (auto& handle : _loaded_tilesets)
    {
        auto tileset = _tilesets.get_object(handle);
        if (!tileset->config->is_visible
            || !tileset->config->visibility_constraints_result.satisfied_in)
            continue;

        for (auto& tile_index : tileset->active_tiles)
        {
            const auto& tile = tileset->tiles.at(tile_index);
            if (tile.load_status() != ThreeDTile::LoadStatus::LOADED
                && tile.load_status() != ThreeDTile::LoadStatus::RENDERABLE
                && tile.load_status() != ThreeDTile::LoadStatus::ERROR)
            {
                return true;
            }

            for (auto& subtile : tile.subtiles)
            {
                if (subtile.styling_status() != ThreeDTile::StylingStatus::IDLE)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

LayerH ThreeDTilesSystem::get_layer_for_tileset(TilesetH tileset_handle)
{
    auto tileset = _tilesets.get_object(tileset_handle);

    if (tileset == nullptr)
    {
        HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
        return 0;
    }

    return tileset->config->layer_handle;
}

std::string ThreeDTilesSystem::get_url_for_tile(TilesetH tileset_handle, uint32_t tile_index)
{
    auto tileset = _tilesets.get_object(tileset_handle);

    if (tileset == nullptr)
    {
        HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
        return "";
    }

    if (tile_index >= tileset->tiles.size())
    {
        HRZ_LOG_WARNING("Invalid tile index for tileset {}: {}", tileset_handle, tile_index);
        return "";
    }

    const auto& tile = tileset->tiles.at(tile_index);

    return tileset->base_url.derive(tile.uri);
}

unsigned int ThreeDTilesSystem::get_attribute_count(TilesetH tileset_handle)
{
    auto tileset = _tilesets.get_object(tileset_handle);

    if (tileset == nullptr)
    {
        HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
        return 0;
    }

    return tileset->config->attributes.size();
}

void ThreeDTilesSystem::destroy(
    hrz::AssetsLoader* al,
    hrz::JobScheduler* js,
    hrz::BlobAllocator* ba,
    hrz::Render* render)
{
    for (const auto handle : _removed_tilesets)
    {
        unload_tileset(handle, al, js, ba);
    }
    _removed_tilesets.clear();

    while (!_loading_tilesets.empty())
    {
        unload_tileset(*_loading_tilesets.begin(), al, js, ba);
    }
    _loading_tilesets.clear();

    while (!_loaded_tilesets.empty())
    {
        unload_tileset(*_loaded_tilesets.begin(), al, js, ba);
    }
    _loaded_tilesets.clear();

    _model_cache.destroy(al, js, ba, _removed_resources);

    for (const auto resource : _removed_resources)
    {
        render->rc->dealloc(resource);
    }
    _removed_resources.clear();

    if (_model_shared_resources != nullptr)
    {
        hrz::model::destroy_shared_resources(_model_shared_resources, render);
    }

    if (_model_shared_resources_instanced != nullptr)
    {
        hrz::model::destroy_shared_resources(_model_shared_resources_instanced, render);
    }

    _vector_data_channel.close();
}

} // namespace hrz::three_d_tiles
