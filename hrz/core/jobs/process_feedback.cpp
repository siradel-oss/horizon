// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/jobs/process_feedback.h"

#include "hrz/common/geo.h"
#include "hrz/common/profiling.h"
#include "hrz/core/jobs/context.h"
#include "hrz/core/jobs/job_result.h"
#include "hrz/fnd/flat_hash_map.h"

using namespace hrz;

namespace hrz_jobs::process_feedback_texture
{

constexpr uint32_t MIN_PIXEL_COUNT = 10;

inline bool is_valid(const TileCoords& coords)
{
    if (coords.lod >= hrz::CLIPMAP_LOD_COUNT)
    {
        return false;
    }

    if (coords.x >= CLIPMAP_SIZE || coords.y >= CLIPMAP_SIZE)
    {
        return false;
    }

    return true;
}

TileCoords tile_parent(TileCoords tile, const std::vector<lm::uvec2>& offsets)
{
    const uint32_t level_size = 1 << tile.lod;

    if (level_size <= CLIPMAP_SIZE)
    {
        tile.x /= 2;
        tile.y /= 2;
    }
    else
    {
        uint32_t real_tile_x = tile.x + offsets[tile.lod].x;
        uint32_t real_tile_y = tile.y + offsets[tile.lod].y;

        real_tile_x >>= 1;
        real_tile_y >>= 1;

        real_tile_x -= offsets[tile.lod - 1].x;
        real_tile_y -= offsets[tile.lod - 1].y;

        tile.x = real_tile_x;
        tile.y = real_tile_y;
    }

    tile.lod -= 1;
    return tile;
}

hrz_jobs::JobResult run(
    const hrz_jobs::FeedbackData& feedback,
    hrz_jobs::TileList& response,
    const JobContext&)
{
    hrz::flat_hash_map<TileCoords, uint32_t> usages;

    HRZ_SCOPED_SAMPLE("process feedback job");

    auto increment_usage = [&](TileCoords tile_coords)
    {
        auto it = usages.find(tile_coords);
        if (it == usages.end())
        {
            usages.insert({tile_coords, 1});
        }
        else
        {
            it->second++;
        }
    };

    {
        HRZ_SCOPED_SAMPLE("count tiles");

        auto image_data = feedback.image.blob().get_data();
        const uint32_t* data_ptr = (const uint32_t*)image_data.data();
        int pixel_count = feedback.image.width() * feedback.image.height();

        for (int i = 0; i < pixel_count; ++i)
        {
            uint32_t value_x = data_ptr[0];
            uint32_t value_y = data_ptr[1];
            uint32_t value_z = data_ptr[2];

            data_ptr += 4;

            auto tile_coords = TileCoords(value_x, value_y, (uint8_t)(value_z - 1));

            if (value_z == 0 || !is_valid(tile_coords)) continue;

            auto feedback_tile_coords = tile_coords;

            // Increment the usage for the tile that was actually requested,
            // but also for the tiles covering the same area at lower zoom
            // levels. This is used to preload these tiles, in order to have
            // smoother transitions when moving the camera.

            while (true)
            {
                // Only increment the usage for even zoom levels, as a compromise
                // between avoid too sharp transitions and not preloading too
                // many tiles.
                // Also include level 1. This is a tiny hack for the commonly used
                // Bing imagery, which does not have a level 0.

                if (tile_coords == feedback_tile_coords || tile_coords.lod % 2 == 0
                    || tile_coords.lod <= 1)
                {
                    increment_usage(tile_coords);
                }

                if (tile_coords.lod == 0) break;

                tile_coords = tile_parent(tile_coords, feedback.clipmap_offsets);
            }
        }
    }

    {
        HRZ_SCOPED_SAMPLE("make vector");

        response.tile_usage.reserve(usages.size());
        for (auto& it : usages)
        {
            if (it.second >= MIN_PIXEL_COUNT)
            {
                response.tile_usage.emplace_back(
                    hrz::planet::RequestedTileCoords{
                        it.first, it.second, hrz::planet::TileRequestOrigin::FeedbackOrigin
                    });
            }
        }
    }

    {
        HRZ_SCOPED_SAMPLE("sort vector");

        std::ranges::sort(
            response.tile_usage, [](const auto& t1, const auto& t2)
            { return tile_usage_comp(t1.coords, t1.uses, 0, t2.coords, t2.uses, 0); });
    }

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::process_feedback_texture
