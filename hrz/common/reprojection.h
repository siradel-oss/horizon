// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/tile_coords.h"
#include "hrz/protocol/raster/tiling_scheme.pb.h"

#include <lin_maths.h>

struct pl_Crs;

namespace hrz
{

namespace planet
{

struct TiledRasterGeometry;

}

// This structure's role is to unify the cases of global and local
// tiling schemes. They are both considered as instances of images
// placed somewhere inside a domain. The domain is tiled, and the
// image is cut in tiles along the same lines.
struct ImageTilingInfo
{
    lm::ilbbox2 image_pixel_bounds;
    lm::ulvec2 image_pixel_size;
    lm::dbbox2 domain_bounds;
    lm::dvec2 domain_bounds_size;
    lm::ulvec2 domain_pixel_size;
    lm::dbbox2 raster_bounds;
    uint8_t min_lod;
    uint8_t max_lod;
    int max_mipmap_level;
    int min_available_mipmap_level;
    int max_available_mipmap_level;
    int lod_offset;
    unsigned int provider_tile_pixel_size;
    unsigned int level_zero_tile_count_x;
    unsigned int level_zero_tile_count_y;
    hrz_proto::TilingOrigin tiling_origin;
    bool tiles_always_full_size;
};

int32_t compute_level_offset(const hrz_proto::TilingSchemeParams& tiling_scheme);

ImageTilingInfo compute_image_tiling_info(
    const hrz::planet::TiledRasterGeometry& raster_params,
    const pl_Crs* image_projection_crs);

// Computes the position in pixels in the raster from the position in the raster's projection.
inline lm::dvec2 pixel_pos_from_proj_pos(const ImageTilingInfo& info, const lm::dvec2& proj_pos)
{
    return {
        std::floor(
            ((proj_pos.x - info.domain_bounds.min.x) / info.domain_bounds_size.x)
            * info.domain_pixel_size.x),
        std::floor(
            ((info.domain_bounds.max.y - proj_pos.y) / info.domain_bounds_size.y)
            * info.domain_pixel_size.y),
    };
}

struct TileCoordsAndUv
{
    TileCoords tile_coords;
    lm::dvec2 uv;
    lm::uvec2 tile_size;
};

std::optional<TileCoordsAndUv> proj_pos_to_tile_uv(
    const lm::dvec2& proj_pos,
    uint8_t lod,
    const ImageTilingInfo& tiling_info);

lm::dvec2 tile_uv_to_proj_pos(
    const TileCoords& tile_coords,
    const lm::dvec2& uv,
    const ImageTilingInfo& tiling_info);

} // namespace hrz
