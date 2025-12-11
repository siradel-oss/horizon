#pragma once

#include "hrz/common/planet/tiled_raster_geometry.h"
#include "hrz/common/tile_coords.h"

namespace hrz_jobs
{

struct RasterTileReprojParams
{
    hrz::TileCoords tile_coords;
    hrz::planet::TiledRasterGeometry raster_geometry;
    // In Web Mercator (EPSG:3857)
    lm::dbbox2 raster_display_bounds;
    uint32_t quad_size{};
};

struct ReprojectedTiles
{
    struct ReprojectedTile
    {
        hrz::TileCoords coords;
        lm::uvec2 grid_size;
        std::vector<float> grid_coords;
        lm::bbox2 uv_clip;
    };

    std::vector<ReprojectedTile> tiles;
};

} // namespace hrz_jobs
