// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/blob_allocator.h"
#include "hrz/common/color.h"
#include "hrz/common/profiling.h"
#include "hrz/common/three_d_tiles.h"
#include "hrz/core/debug_draw.h"
#include "hrz/core/global_flags.h"
#include "hrz/core/job_scheduler.h"
#include "hrz/core/model/model.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/scene.h"
#include "hrz/core/shadows.h"
#include "hrz/core/sky.h"
#include "hrz/core/three_d_tiles/model_cache.h"
#include "hrz/core/three_d_tiles/system.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/core/vector/data_loader/data_loader.h"
#include "hrz/fnd/meta.h"

#include <cassert>
#include <optional>
#include <variant>
#include <vector>

namespace hrz::three_d_tiles
{

namespace
{

void draw_debug_box(const BoundingVolume& volume)
{
    lm::dvec3 center;
    lm::dvec3 half_u;
    lm::dvec3 half_v;
    lm::dvec3 half_w;

    const auto set_from_box = [&](const BoundingVolume::Box& box)
    {
        center = box.center;
        half_u = box.u_axis * box.u_half_length;
        half_v = box.v_axis * box.v_half_length;
        half_w = box.w_axis * box.w_half_length;
    };

    const auto set_from_sphere = [&](const BoundingVolume::Sphere& sphere)
    {
        center = sphere.center;
        half_u = {sphere.radius, 0, 0};
        half_v = {0, sphere.radius, 0};
        half_w = {0, 0, sphere.radius};
    };

    if (const auto* region = std::get_if<BoundingVolume::Region>(&volume.volume))
    {
        if (region->box.has_value())
            set_from_box(region->box.value());
        else if (region->sphere.has_value())
            set_from_sphere(region->sphere.value());
        else
            return;
    }
    else if (const auto* box = std::get_if<BoundingVolume::Box>(&volume.volume))
    {
        set_from_box(*box);
    }
    else if (const auto* sphere = std::get_if<BoundingVolume::Sphere>(&volume.volume))
    {
        set_from_sphere(*sphere);
    }
    else
    {
        return;
    }

    const auto c = [&](double u, double v, double w)
    { return center + half_u * u + half_v * v + half_w * w; };

    const lm::vec3 rgb =
        lm::vec3(
            hrz::convert_uint_color_to_bytes((uint32_t)hrz::three_d_tiles::compute_hash(volume))
                .rgb)
        / 255.0F;
    const hrz::DebugDraw dd(lm::vec4(rgb, 1.0F));

    dd.polyline({c(-1, -1, -1), c(1, -1, -1), c(1, 1, -1), c(-1, 1, -1), c(-1, -1, -1)});
    dd.polyline({c(-1, -1, 1), c(1, -1, 1), c(1, 1, 1), c(-1, 1, 1), c(-1, -1, 1)});
    dd.polyline({c(-1, -1, -1), c(-1, -1, 1)});
    dd.polyline({c(1, -1, -1), c(1, -1, 1)});
    dd.polyline({c(1, 1, -1), c(1, 1, 1)});
    dd.polyline({c(-1, 1, -1), c(-1, 1, 1)});
}

} // anonymous namespace

ContentRenderContext ThreeDTilesSystem::make_content_render_context(hrz::Render* render) const
{
    return ContentRenderContext{
        .render = render,
        .model_shared_resources = _model_shared_resources,
        .model_shared_resources_instanced = _model_shared_resources_instanced,
    };
}

hrz::RenderRequest ThreeDTilesSystem::work_gpu(hrz::Render* render, hrz::BlobAllocator* ba)
{
    HRZ_SCOPED_SAMPLE("3D tiles system work gpu");
    assert(_model_shared_resources != nullptr && _model_shared_resources_instanced != nullptr);

    // Content GPU work allocates from the blob allocator cached in `_content_work_ctx`.
    assert(ba == _content_work_ctx.ba);

    const ContentRenderContext render_ctx = make_content_render_context(render);

    hrz::RenderRequest render_request;

    for (const auto resource : _removed_resources)
    {
        render->rc->dealloc(resource);
    }
    _removed_resources.clear();

    const bool draw_debug_boxes = hrz::get_flag(hrz::Flag::DebugDraw3DTilesBoundingVolumes);

    for (const auto handle : _loaded_tilesets)
    {
        auto tileset = _tilesets.get_object(handle);

        if (!tileset->config->is_visible) continue;

        assert(tileset->status == Tileset::Status::LOADED);

        for (const auto tile_index : tileset->active_tiles)
        {
            auto& tile = tileset->tiles.at(tile_index);

            bool has_unstyled_subtiles = false;

            for (auto& subtile : tile.subtiles)
            {
                // Style data must be set before the content's GPU work,
                // which is where i3dm uploads its colors.
                if (subtile.styling_status() != ThreeDTile::StylingStatus::IDLE)
                {
                    render_request |= subtile.upload_style_data();
                }

                render_request |=
                    subtile.work_gpu(_content_work_ctx, render_ctx, tile, tileset->config);

                if (!subtile.is_styled()) has_unstyled_subtiles = true;
            }

            // The tile has finished loading. We only make it renderable
            // once it is styled, so as to avoid having a restyling a few
            // frames later, which would create undesired flickering.
            if (tile.load_status() == ThreeDTile::LoadStatus::LOADED && !has_unstyled_subtiles)
            {
                render_request |= tileset->make_tile_renderable(tile_index);
            }

            if (draw_debug_boxes && tileset->render
                && tile.load_status() == ThreeDTile::LoadStatus::RENDERABLE
                && tile.draw_in(*tileset->config).any())
            {
                draw_debug_box(tile.bounding_volume);
            }
        }
    }

    return render_request;
}

void ThreeDTilesSystem::draw(hrz::Render* render, hrz::AttributionRegistry* attributions)
{
    HRZ_SCOPED_SAMPLE("3D tiles system draw");

    const ContentRenderContext render_ctx = make_content_render_context(render);

    for (const auto handle : _loaded_tilesets)
    {
        auto tileset = _tilesets.get_object(handle);
        auto config = tileset->config;

        if (!tileset->render || !config->is_visible) continue;

        assert(tileset->status == Tileset::Status::LOADED);

        for (const auto tile_index : tileset->active_tiles)
        {
            auto& tile = tileset->tiles.at(tile_index);

            if (tile.load_status() != ThreeDTile::LoadStatus::RENDERABLE) continue;

            hrz::SceneViewBitset draw_in = tile.draw_in(*config);

            if (!draw_in.any()) continue;

            for (auto& subtile : tile.subtiles)
            {
                if (subtile.attribute_attributions)
                {
                    hrz::attribution::use_this_frame(attributions, subtile.attribute_attributions);
                }

                std::visit(
                    hrz::overload{
                        [&](ContentSubtile auto& content)
                        { content.draw(_content_work_ctx, render_ctx, draw_in); },
                        [&](ThreeDTile::TilesetContent&)
                        {
                            // No action needed for these types
                        },
                        [&](std::monostate&)
                        {
                            // No action needed for these types
                        }
                    },
                    subtile.content);
            }
        }
    }
}

} // namespace hrz::three_d_tiles
