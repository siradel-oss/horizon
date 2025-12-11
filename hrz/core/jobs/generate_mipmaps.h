#pragma once

#include "hrz/common/blob_image.h"
#include "hrz/common/tile_coords.h"
#include "hrz/protocol/raster/nodata.pb.h"

#include <lin_maths.h>

#include <vector>

namespace hrz_jobs
{

struct MipmapGenerationParams
{
    hrz::BlobImage image;
    uint32_t tile_size;
    hrz_proto::RasterNodata nodata;
};

struct Mipmaps
{
    struct Tile
    {
        hrz::TileCoords coords;
        hrz::BlobImage image;
    };

    std::vector<Tile> tiles;
};

} // namespace hrz_jobs
