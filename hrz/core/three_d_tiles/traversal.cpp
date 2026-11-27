// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/horizon_culling.h"
#include "hrz/common/profiling.h"
#include "hrz/common/three_d_tiles.h"
#include "hrz/core/clock.h"
#include "hrz/core/debug_draw.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/screen_space.h"
#include "hrz/core/three_d_tiles/system.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/core/vector/data_loader/data_loader.h"
#include "hrz/core/visibility_constraints.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/static_vector.h"

#include <array>
#include <cassert>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace hrz::three_d_tiles
{

namespace
{

constexpr double REQUEST_PRIORITY_UPDATE_INTERVAL = 500; // ms

/**
 * Compute the screen-space used to determine whether the root tile
 * should be rendered or not.
 */
hrz::StaticVector<double, hrz::SCENE_VIEW_COUNT> compute_root_screen_space_errors(
    const ThreeDTile& root_tile,
    double root_geometric_error,
    std::span<const hrz::RenderViewInfo> views_info,
    std::span<const hrz::render::ScreenSpaceError> sses,
    double max_screen_space_error)
{
    HRZ_SCOPED_SAMPLE_A("compute root screen space error");

    hrz::StaticVector<double, hrz::SCENE_VIEW_COUNT> errors;
    assert(views_info.size() == sses.size());

    for (size_t i = 0; i < views_info.size(); ++i)
    {
        const double distance = hrz::three_d_tiles::distance(
            root_tile.bounding_volume, views_info[i].cam_view_info.cam.pos);
        const double error = sses[i].compute_screen_space_error(
            root_geometric_error, distance, max_screen_space_error);

        errors.push_back(error);
    }

    return errors;
}

/**
 * Compute the screen-space used to determine whether a tile should
 * be refined or not.
 */
hrz::StaticVector<double, hrz::SCENE_VIEW_COUNT> compute_tile_screen_space_errors(
    const ThreeDTile& tile,
    std::span<const hrz::RenderViewInfo> views_info,
    std::span<const hrz::render::ScreenSpaceError> sses,
    double max_screen_space_error)
{
    HRZ_SCOPED_SAMPLE_A("compute tile screen space error");

    hrz::StaticVector<double, hrz::SCENE_VIEW_COUNT> errors;

    // If the tile cannot be refined, don't bother computing its screen-space error.
    if (tile.geometric_error == 0)
    {
        for (size_t i = 0; i < views_info.size(); ++i)
        {
            errors.push_back(0);
        }
    }
    else
    {
        assert(views_info.size() == sses.size());

        for (size_t i = 0; i < views_info.size(); ++i)
        {
            const double distance = hrz::three_d_tiles::distance(
                tile.bounding_volume, views_info[i].cam_view_info.cam.pos);
            const double error = sses[i].compute_screen_space_error(
                tile.geometric_error, distance, max_screen_space_error);

            errors.push_back(error);
        }
    }

    return errors;
}

hrz::StaticVector<std::array<lm::dvec4, 4>, hrz::SCENE_VIEW_COUNT> compute_views_planes(
    std::span<const hrz::RenderViewInfo> views_info)
{
    // Compute planes for the view.
    // See https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/

    // Do not compute the Z view planes, as we don't want to cull using
    // the near and far planes. (They change depending on what is
    // displayed, and here we can load tiles that will affect them.)

    hrz::StaticVector<std::array<lm::dvec4, 4>, hrz::SCENE_VIEW_COUNT> planes;

    for (const auto& view_info : views_info)
    {
        const lm::dmat4 pv_t = lm::transpose(view_info.cam_view_info.pv);

        const auto normalize = [](const lm::dvec4& plane) { return plane / lm::length(plane.xyz); };

        planes.push_back(
            {normalize(-pv_t.w - pv_t.x), normalize(-pv_t.w + pv_t.x), normalize(-pv_t.w - pv_t.y),
             normalize(-pv_t.w + pv_t.y)});
    }

    return planes;
}

hrz::StaticVector<std::array<lm::dvec3, 5>, hrz::SCENE_VIEW_COUNT> compute_views_vertices(
    std::span<const hrz::RenderViewInfo> views_info)
{
    hrz::StaticVector<std::array<lm::dvec3, 5>, hrz::SCENE_VIEW_COUNT> vertices;

    // Reduce the near plane to a single position.

    for (const auto& view_info : views_info)
    {
        const auto clip_to_world = [&](double x, double y, double z)
        {
            const auto p = (view_info.cam_view_info.inv_pv * lm::dvec4(lm::dvec3(x, y, z), 1.0));
            return p.xyz / p.w;
        };

        vertices.push_back({
            clip_to_world(-1.0, -1.0, 1.0),
            clip_to_world(1.0, -1.0, 1.0),
            clip_to_world(1.0, 1.0, 1.0),
            clip_to_world(1.0, 1.0, 1.0),
            view_info.cam_view_info.cam.pos,
        });
    }

    return vertices;
}

hrz::SceneViewBitset compute_space_subset_intersections(
    std::span<const hrz::RenderViewInfo> views_info,
    const BoundingVolume& volume,
    std::span<const std::array<lm::dvec4, 4>> views_planes,
    std::span<const std::array<lm::dvec3, 5>> views_vertices)
{
    hrz::SceneViewBitset bitset;

    HRZ_SCOPED_SAMPLE_A("compute space subset intersections");

    for (size_t i = 0; i < views_planes.size(); ++i)
    {
        if (hrz::three_d_tiles::intersects_space_subset(volume, views_planes[i], views_vertices[i]))
        {
            bitset.set((int)views_info[i].view);
        }
    }

    return bitset;
}

void threshold_screen_space_error(
    std::span<const hrz::RenderViewInfo> views_info,
    std::span<const double> sses,
    hrz::SceneViewBitset* below_min_screen_space_error_in,
    hrz::SceneViewBitset* above_max_screen_space_error_in,
    TilesetConfig::ScreenSpaceErrorHysteresis screen_space_error_hysteresis)
{
    assert(screen_space_error_hysteresis.min_error <= screen_space_error_hysteresis.max_error);

    below_min_screen_space_error_in->reset();
    above_max_screen_space_error_in->reset();

    for (size_t i = 0; i < sses.size(); ++i)
    {
        if (sses[i] < screen_space_error_hysteresis.min_error)
        {
            below_min_screen_space_error_in->set((int)views_info[i].view);
        }
        else if (sses[i] >= screen_space_error_hysteresis.max_error)
        {
            above_max_screen_space_error_in->set((int)views_info[i].view);
        }
    }
}

void test_horizon_occlusion(
    std::span<const hrz::RenderViewInfo> views_info,
    std::span<const hrz::HorizonCuller> cullers,
    const std::optional<lm::dvec3>& horizon_occlusion_point,
    hrz::SceneViewBitset* occluded_by_horizon_in)
{
    occluded_by_horizon_in->reset();

    if (!horizon_occlusion_point.has_value())
    {
        return;
    }

    for (size_t i = 0; i < cullers.size(); ++i)
    {
        if (cullers[i].is_occluded(horizon_occlusion_point.value()))
        {
            occluded_by_horizon_in->set(views_info[i].view);
        }
    }
}

struct TileVisibility
{
    const ThreeDTile& tile;
    const TilesetWorkContext& ctx;

    hrz::SceneViewBitset above_max_screen_space_error_in;
    hrz::SceneViewBitset below_min_screen_space_error_in;
    hrz::SceneViewBitset occluded_by_horizon_in;

    std::optional<hrz::SceneViewBitset> space_subset_intersections;

    hrz::SceneViewBitset intersects_space_subset_in()
    {
        if (!space_subset_intersections.has_value())
        {
            space_subset_intersections = compute_space_subset_intersections(
                ctx.views_info, tile.bounding_volume, ctx.views_planes, ctx.views_vertices);
        }

        return space_subset_intersections.value();
    }

    // Views in which the tile is big enough on screen to need its children, and visible.
    hrz::SceneViewBitset should_refine_in(hrz::SceneViewBitset visiting_in)
    {
        return visiting_in & above_max_screen_space_error_in & !occluded_by_horizon_in
            & intersects_space_subset_in();
    }

    // Views in which the tile no longer needs its children: it is small enough on screen,
    // hidden by the horizon, or out of the view.
    hrz::SceneViewBitset should_unrefine_in(hrz::SceneViewBitset visiting_in)
    {
        return visiting_in
            & (below_min_screen_space_error_in | occluded_by_horizon_in
               | !intersects_space_subset_in());
    }
};

TileVisibility compute_tile_visibility(
    const ThreeDTile& tile,
    std::span<const double> screen_space_errors,
    const TilesetConfig& config,
    const TilesetWorkContext& ctx)
{
    TileVisibility visibility{.tile = tile, .ctx = ctx};

    threshold_screen_space_error(
        ctx.views_info, screen_space_errors, &visibility.below_min_screen_space_error_in,
        &visibility.above_max_screen_space_error_in, config.screen_space_error_hysteresis);

    test_horizon_occlusion(
        ctx.views_info, ctx.horizon_cullers, tile.horizon_occlusion_point,
        &visibility.occluded_by_horizon_in);

    const bool debug_draw_horizon_occlusion_points =
        hrz::get_flag(hrz::Flag::DebugDrawHorizonOcclusionPoints);
    if (debug_draw_horizon_occlusion_points && tile.horizon_occlusion_point.has_value())
    {
        static constexpr lm::vec4 OccludedColor = {0.7F, 0.7F, 0.7F, 1};
        static constexpr lm::vec4 VisibleColor = {1, 0, 0, 1};

        hrz::debug_draw::points(
            tile.horizon_occlusion_point.value().m,
            visibility.occluded_by_horizon_in.any() ? OccludedColor : VisibleColor,
            hrz::debug_draw::Space::Ecef, hrz::debug_draw::Group_Vector);
    }

    return visibility;
}

void update_refinement(
    Tileset* tileset,
    uint32_t tile_index,
    ThreeDTile& tile,
    TileVisibility& visibility,
    const TilesetWorkContext& ctx,
    std::vector<uint32_t>* tiles_to_refine)
{
    // Apply a bit of hysteresis on the [un]refinement decision.
    // The values come from suggestions on the Cesium bug trackers,
    // such as here: https://github.com/CesiumGS/cesium/issues/3279
    if (tile.child_count > 0
        && (tile.is_empty_structural || visibility.above_max_screen_space_error_in.any()))
    {
        hrz::SceneViewBitset should_refine_in = tile.is_empty_structural
            ? ctx.visiting_in
            : visibility.should_refine_in(ctx.visiting_in);

        // Don't refine tiles that have a viewer bounding volume we're not inside of.
        if (tile.viewer_bounding_volume.has_value())
        {
            const auto& bounding_volume = tile.viewer_bounding_volume.value();

            for (const auto& view_info : ctx.views_info)
            {
                const bool is_outside_viewer_volume =
                    hrz::three_d_tiles::distance(bounding_volume, view_info.cam_view_info.cam.pos)
                    > 0;

                if (is_outside_viewer_volume)
                {
                    should_refine_in.reset((int)view_info.view);
                }
            }
        }

        // @Todo Hide when outside viewer volume?

        if (should_refine_in.any())
        {
            if (tile.active_child_count() < tile.child_count)
            {
                tiles_to_refine->push_back(tile_index);
            }

            tile.should_render_children_in |= should_refine_in;
        }
    }

    if (!tile.is_empty_structural && (tile.should_render_children_in & ctx.visiting_in).any())
    {
        const auto should_unrefine_in = visibility.should_unrefine_in(ctx.visiting_in);

        if (should_unrefine_in.any())
        {
            // Unrefine tiles
            tile.should_render_children_in &= !should_unrefine_in;

            if (tile.should_render_children_in.none())
            {
                for (int i = 0; i < tile.child_count; ++i)
                {
                    const auto child_tile_index =
                        tileset->child_links.at(tile.first_child_link_index + i);
                    auto& child_tile = tileset->tiles.at(child_tile_index);

                    child_tile.delete_if_unused = true;
                }
            }
        }
    }
}

void adjust_parent_rendered_child_count(
    Tileset* tileset,
    uint32_t tile_index,
    const ThreeDTile& tile,
    hrz::SceneViewBitset views,
    int delta)
{
    if (tile_index == tileset->root_tile_index) return;

    auto& parent_tile = tileset->tiles.at(tile.parent_index);

    for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
    {
        if (views.is_set(i))
        {
            parent_tile.rendered_child_count_per_view[i] += delta;
            assert(parent_tile.rendered_child_count_per_view[i] >= 0);
            assert(parent_tile.rendered_child_count_per_view[i] <= parent_tile.child_count);
        }
    }
}

void update_culling(ThreeDTile& tile, TileVisibility& visibility)
{
    // Perform culling if the tile is set to render itself
    if (tile.render_self_in.any())
    {
        tile.culled_in =
            (!visibility.intersects_space_subset_in()) | visibility.occluded_by_horizon_in;
    }
    else
    {
        // If we are not trying to draw the tile, the culling bitset should be reset.
        // Otherwise, flickering would occur in situations where a parent tile asks for
        // a child to be drawn, but the work on that child has already been carried out
        // for the current frame, meaning that it is stuck with whatever cull state it
        // has been left in. If that state was "culled", then the child would not be
        // drawn until the next frame, leaving a hole during the current one.
        tile.culled_in.reset();
    }
}

} // anonymous namespace

hrz::RenderRequest ThreeDTilesSystem::update_visibility_constraints(
    std::span<const hrz::RenderViewInfo> views_info)
{
    if (hrz::get_flag(hrz::Flag::DebugFreeze3DTilesCulling)) return {};

    hrz::RenderRequest render_request;

    for (const auto handle : _root_tilesets)
    {
        render_request |=
            _tilesets.get_object(handle)->get_mutable_config()->update_visibility_constraints(
                views_info);
    }

    return render_request;
}

hrz::RenderRequest ThreeDTilesSystem::work_loaded_tilesets(const WorkContext& ctx)
{
    HRZ_SCOPED_SAMPLE("work loaded tilesets");

    if (hrz::get_flag(hrz::Flag::DebugFreeze3DTilesCulling)) return {};

    hrz::RenderRequest render_request;

    hrz::StaticVector<hrz::render::ScreenSpaceError, hrz::SCENE_VIEW_COUNT> sses;
    hrz::StaticVector<hrz::HorizonCuller, hrz::SCENE_VIEW_COUNT> horizon_cullers;

    for (const auto& view_info : ctx.views_info)
    {
        sses.push_back(
            hrz::render::ScreenSpaceError(
                view_info.cam_view_info.cam.fovy, (double)view_info.cam_view_info.viewport.size.y,
                view_info.cam_view_info.viewport.device_pixel_ratio));
        horizon_cullers.push_back(hrz::HorizonCuller(view_info.cam_view_info.cam.pos));
    }

    auto views_planes = compute_views_planes(ctx.views_info);
    auto views_vertices = compute_views_vertices(ctx.views_info);

    // Tile download priorities are computed using their relative position
    // from the camera. Because the camera can move, the priorities can
    // become outdated, so we update them regularly.
    bool should_update_priorities = false;
    {
        const double duration = hrz::clock::CurrentFrameWallTime.ms - _request_priority_timer_ms;
        if (duration >= REQUEST_PRIORITY_UPDATE_INTERVAL)
        {
            should_update_priorities = true;
        }
    }

    TilesetWorkContext tileset_ctx(ctx);
    tileset_ctx.sses = sses;
    tileset_ctx.horizon_cullers = horizon_cullers;
    tileset_ctx.views_planes = views_planes;
    tileset_ctx.views_vertices = views_vertices;
    tileset_ctx.should_update_priorities = should_update_priorities;

    for (const auto handle : _loaded_tilesets)
    {
        auto* tileset = _tilesets.get_object(handle);
        const auto* config = tileset->config;
        tileset_ctx.visiting_in =
            config->visibility_constraints_result.satisfied_in & config->scene_views_bitset;
        render_request |= work_loaded_tileset(tileset, handle, tileset_ctx);
    }

    return render_request;
}

hrz::RenderRequest ThreeDTilesSystem::work_loaded_tileset(
    Tileset* tileset,
    TilesetH tileset_handle,
    const TilesetWorkContext& ctx)
{
    hrz::RenderRequest render_request;

    const auto* config = tileset->config;

    if (!config->is_visible || !config->visibility_constraints_result.satisfied_in)
        return render_request;

    assert(tileset->status == Tileset::Status::LOADED);

    if (tileset->is_root() && config->renew_vector_data_layer_request)
    {
        auto* mut_config = tileset->get_mutable_config();

        if (mut_config->vector_data_layer_status
            != TilesetConfig::VectorDataLayerStatus::UNREQUESTED)
        {
            _vector_data_channel.send(
                hrz::vector_data::messages::ReleaseLayerLoader{
                    mut_config->vector_data_layer_request_id
                });
        }

        _vector_data_channel.send(
            hrz::vector_data::messages::LoadLayer{
                mut_config->vector_data_layer_request_id, mut_config->vector_data_layer_id
            });
        mut_config->vector_data_layer_status = TilesetConfig::VectorDataLayerStatus::LOADING;

        mut_config->renew_vector_data_layer_request = false;
    }

    for (const auto tile_index : tileset->active_tiles)
    {
        render_request |= work_tile_content(tileset, tileset_handle, tile_index, ctx);
    }

    render_request |= work_root_tile_refinement(tileset, ctx);

    // We have to refine after iterating over the active tiles,
    // otherwise we would be insert into the set while iterating on it,
    // which is illegal because it invalidates the iterators.
    std::vector<uint32_t> tiles_to_refine;

    for (auto it = tileset->active_tiles.begin(); it != tileset->active_tiles.end();)
    {
        const auto [traversal_render_request, erase] =
            work_tile_traversal(tileset, *it, &tiles_to_refine, ctx);
        render_request |= traversal_render_request;

        if (erase)
        {
            tileset->active_tiles.erase(it++);
        }
        else
        {
            ++it;
        }
    }

    for (const auto tile_index : tiles_to_refine)
    {
        const auto& tile = tileset->tiles.at(tile_index);

        // Refine tile
        for (int i = 0; i < tile.child_count; ++i)
        {
            const auto child_tile_index = tileset->child_links.at(tile.first_child_link_index + i);
            auto& child_tile = tileset->tiles.at(child_tile_index);

            if (child_tile.load_status() == ThreeDTile::LoadStatus::UNLOADED)
            {
                load_tile(tileset, child_tile_index, child_tile, ctx);
            }
        }
    }

#if HRZ_DEBUG
    for (const auto tile_index : tileset->active_tiles)
    {
        tileset->check_tile_invariants(tileset->tiles.at(tile_index));
    }
#endif

    return render_request;
}

// The root tile has no parent tile, so this plays the parent's role for it: deciding whether
// the root tile is needed, loading it, and setting its render status. The root tile's own
// traversal is then done by `work_tile_traversal`, like for any other tile.
hrz::RenderRequest ThreeDTilesSystem::work_root_tile_refinement(
    Tileset* tileset,
    const TilesetWorkContext& ctx)
{
    hrz::RenderRequest render_request;
    hrz::SceneViewBitset root_tile_needed_in = false;
    const auto* config = tileset->config;

    auto& root_tile = tileset->tiles.at(tileset->root_tile_index);
    const auto screen_space_errors = compute_root_screen_space_errors(
        root_tile, tileset->geometric_error, ctx.views_info, ctx.sses,
        config->max_screen_space_error);
    auto visibility = compute_tile_visibility(root_tile, screen_space_errors, *config, ctx);

    root_tile.forget_inactive_views(ctx.visiting_in);

    // If a tileset is standalone, we can wait until the root tile is
    // close and visible before loading and displaying it.
    // However, if it is an external tileset, the decision to load and
    // display the root tile belongs to the parent tileset. This is
    // controlled through the `render` value on the `Tileset` struct.
    if (!tileset->is_root())
    {
        root_tile_needed_in |= ctx.visiting_in;
    }
    else
    {
        if (root_tile.load_status() == ThreeDTile::LoadStatus::UNLOADED)
        {
            root_tile_needed_in |= visibility.should_refine_in(ctx.visiting_in);
        }
        else
        {
            const auto not_needed_in = visibility.should_unrefine_in(ctx.visiting_in);

            if (not_needed_in.all_of(ctx.visiting_in))
            {
                root_tile.delete_if_unused = true;
            }

            root_tile_needed_in |= ctx.visiting_in & !not_needed_in;
        }
    }

    if (root_tile_needed_in.any() && root_tile.load_status() == ThreeDTile::LoadStatus::UNLOADED)
    {
        load_tile(tileset, tileset->root_tile_index, root_tile, ctx);
    }

    if (root_tile.load_status() == ThreeDTile::LoadStatus::RENDERABLE)
    {
        render_request |= set_render_self_in(
            root_tile, root_tile.render_none_in() & root_tile_needed_in & ctx.visiting_in);
    }

    render_request |= set_render_none_in(
        root_tile, root_tile.render_self_in & (!root_tile_needed_in) & ctx.visiting_in);

    return render_request;
}

hrz::RenderRequest ThreeDTilesSystem::work_tile_content(
    Tileset* tileset,
    TilesetH tileset_handle,
    uint32_t tile_index,
    const TilesetWorkContext& ctx)
{
    auto& tile = tileset->tiles.at(tile_index);

    for (auto& subtile : tile.subtiles)
    {
        subtile.work(_content_work_ctx);
    }

    hrz::RenderRequest render_request;
    render_request |= work_tile_load_status(tileset, tileset_handle, tile_index, tile, ctx);
    render_request |= work_tile_styling(tileset, tile, ctx);
    return render_request;
}

std::pair<hrz::RenderRequest, bool> ThreeDTilesSystem::work_tile_traversal(
    Tileset* tileset,
    uint32_t tile_index,
    std::vector<uint32_t>* tiles_to_refine,
    const TilesetWorkContext& ctx)
{
    const auto& config = *tileset->config;
    auto& tile = tileset->tiles.at(tile_index);

    hrz::RenderRequest render_request;

    tile.forget_inactive_views(ctx.visiting_in);

    const auto screen_space_errors = compute_tile_screen_space_errors(
        tile, ctx.views_info, ctx.sses, config.max_screen_space_error);
    auto visibility = compute_tile_visibility(tile, screen_space_errors, config, ctx);
    update_refinement(tileset, tile_index, tile, visibility, ctx, tiles_to_refine);

    if (unload_tile_if_unused(tileset, tile, ctx))
    {
        return std::make_pair(render_request, true);
    }

    render_request |= start_rendering_children(tileset, tile_index, tile, ctx.visiting_in);
    render_request |= stop_rendering_children(tileset, tile_index, tile, ctx.visiting_in);

    update_culling(tile, visibility);

    return std::make_pair(render_request, false);
}

bool ThreeDTilesSystem::unload_tile_if_unused(
    Tileset* tileset,
    ThreeDTile& tile,
    const TilesetWorkContext& ctx)
{
    if (!tile.delete_if_unused || !tile.render_none_in().all_of(ctx.visiting_in)) return false;

    unload_tile(tileset, tile, ctx.al, ctx.js, ctx.ba);

    return true;
}

hrz::RenderRequest ThreeDTilesSystem::start_rendering_children(
    Tileset* tileset,
    uint32_t tile_index,
    ThreeDTile& tile,
    hrz::SceneViewBitset visiting_in)
{
    hrz::RenderRequest render_request;

    // Set children to render if they should be rendered
    // (wrt the distance to the camera), if they are available,
    // and if this tile is currently set to render itself.
    if (tile.renderable_child_count() == tile.child_count)
    {
        const hrz::SceneViewBitset start_rendering_children_in =
            tile.render_only_self_in() & tile.should_render_children_in & visiting_in;

        if (start_rendering_children_in.any())
        {
            assert(tile.child_count > 0);

            for (int i = 0; i < tile.child_count; ++i)
            {
                const auto child_tile_index =
                    tileset->child_links.at(tile.first_child_link_index + i);
                auto& child_tile = tileset->tiles.at(child_tile_index);

                render_request |= set_render_self_in(child_tile, start_rendering_children_in);
            }

            for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
            {
                if (start_rendering_children_in.is_set(i))
                {
                    tile.rendered_child_count_per_view[i] = tile.child_count;
                }
            }

            switch (tile.refinement_type)
            {
                case hrz::three_d_tiles::RefinementType::REPLACE:
                    render_request |= set_render_children_in(tile, start_rendering_children_in);
                    break;
                case hrz::three_d_tiles::RefinementType::ADD:
                    render_request |=
                        set_render_self_and_children_in(tile, start_rendering_children_in);
                    break;
                default: assert(false && "Unhandled case"); break;
            }

            adjust_parent_rendered_child_count(
                tileset, tile_index, tile, start_rendering_children_in, -1);
        }
    }

    return render_request;
}

hrz::RenderRequest ThreeDTilesSystem::stop_rendering_children(
    Tileset* tileset,
    uint32_t tile_index,
    ThreeDTile& tile,
    hrz::SceneViewBitset visiting_in)
{
    hrz::RenderRequest render_request;

    // Stop rendering children when we don't need to anymore
    // Check if no grand-children are currently rendered.
    hrz::SceneViewBitset all_child_are_rendered_in;
    for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
    {
        if (visiting_in.is_set(i) && tile.rendered_child_count_per_view[i] == tile.child_count)
        {
            all_child_are_rendered_in.set(i);
        }
    }

    const hrz::SceneViewBitset stop_rendering_children_in = (!tile.should_render_children_in)
        & tile.render_children_in & all_child_are_rendered_in & visiting_in;

    if (stop_rendering_children_in.any())
    {
        for (int i = 0; i < tile.child_count; ++i)
        {
            const auto child_tile_index = tileset->child_links.at(tile.first_child_link_index + i);
            auto& child_tile = tileset->tiles.at(child_tile_index);

            render_request |= set_render_none_in(child_tile, stop_rendering_children_in);
        }

        render_request |= set_render_self_in(tile, stop_rendering_children_in);

        for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
        {
            if (stop_rendering_children_in.is_set(i))
            {
                tile.rendered_child_count_per_view[i] = 0;
            }
        }

        // If we started rendering self and it was not at all
        // drawn before, notify the parent.
        adjust_parent_rendered_child_count(
            tileset, tile_index, tile, stop_rendering_children_in, +1);
    }

    return render_request;
}

hrz::RenderRequest ThreeDTilesSystem::on_render_status_set(
    ThreeDTile& tile,
    hrz::SceneViewBitset render_self_in_before,
    hrz::SceneViewBitset render_children_in_before)
{
    if (tile.render_self_in == render_self_in_before
        && tile.render_children_in == render_children_in_before)
    {
        return {};
    }

    update_external_tilesets_render_status(tile);
    return hrz::RenderRequest::visual();
}

hrz::RenderRequest ThreeDTilesSystem::set_render_self_in(
    ThreeDTile& tile,
    hrz::SceneViewBitset views)
{
    const auto render_self_in_before = tile.render_self_in;
    const auto render_children_in_before = tile.render_children_in;
    tile.set_render_self_in(views);
    return on_render_status_set(tile, render_self_in_before, render_children_in_before);
}

hrz::RenderRequest ThreeDTilesSystem::set_render_children_in(
    ThreeDTile& tile,
    hrz::SceneViewBitset views)
{
    const auto render_self_in_before = tile.render_self_in;
    const auto render_children_in_before = tile.render_children_in;
    tile.set_render_children_in(views);
    return on_render_status_set(tile, render_self_in_before, render_children_in_before);
}

hrz::RenderRequest ThreeDTilesSystem::set_render_self_and_children_in(
    ThreeDTile& tile,
    hrz::SceneViewBitset views)
{
    const auto render_self_in_before = tile.render_self_in;
    const auto render_children_in_before = tile.render_children_in;
    tile.set_render_self_and_children_in(views);
    return on_render_status_set(tile, render_self_in_before, render_children_in_before);
}

hrz::RenderRequest ThreeDTilesSystem::set_render_none_in(
    ThreeDTile& tile,
    hrz::SceneViewBitset views)
{
    const auto render_self_in_before = tile.render_self_in;
    const auto render_children_in_before = tile.render_children_in;
    tile.set_render_none_in(views);
    return on_render_status_set(tile, render_self_in_before, render_children_in_before);
}

void ThreeDTilesSystem::update_external_tilesets_render_status(ThreeDTile& tile)
{
    for (const auto& subtile : tile.subtiles)
    {
        if (const auto* content = std::get_if<ThreeDTile::TilesetContent>(&subtile.content);
            content && content->tileset_handle != 0)
        {
            auto external_tileset = _tilesets.get_object(content->tileset_handle);
            external_tileset->render = tile.render_self_in.any();
        }
    }
}

} // namespace hrz::three_d_tiles
