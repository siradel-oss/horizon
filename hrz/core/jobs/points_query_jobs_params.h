// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_image.h"
#include "hrz/common/planet/elevation_query.h"
#include "hrz/common/planet/tiled_raster_geometry.h"
#include "hrz/common/tile_coords.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/protocol/raster/blending.pb.h"
#include "hrz/protocol/raster/sampling.pb.h"

#include <lin_maths.h>

namespace hrz_jobs
{

struct SamplePointsQueryParams
{
    struct Raster
    {
        hrz_proto::ImageFormat image_format;
        hrz::planet::TiledRasterGeometry geometry;
        // In Web Mercator (EPSG:3857)
        lm::dbbox2 display_bounds;
        hrz_proto::RasterNodata nodata;
        hrz_proto::RasterBlending blending;
        hrz_proto::RasterSampling sampling;
    };

    struct TileWithImage
    {
        uint32_t raster_index;
        hrz::TileCoords tile_coords;
        hrz::BlobImage image;
    };

    std::vector<Raster> rasters;
    hrz::planet::ElevationQueryPointStorage points;
    std::vector<TileWithImage> tiles;
};

struct SampledPointsQuery
{
    hrz::BlobArray<float> values;
};

struct CullPointsQueryParams
{
    struct Raster
    {
        hrz::planet::TiledRasterGeometry geometry;
        // In Web Mercator (EPSG:3857)
        lm::dbbox2 display_bounds;
    };

    hrz::InlinedVector<Raster, 4> rasters;
    hrz::planet::ElevationQueryPointStorage points;
};

struct CulledPointsQuery
{
    struct Tile
    {
        uint32_t raster_index;
        hrz::TileCoords coords;
    };

    hrz::InlinedVector<Tile, 16> tiles;
};

} // namespace hrz_jobs
