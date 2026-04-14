#include "hrz/common/reprojection.h"

#include "hrz/common/planet/tiled_raster_geometry.h"
#include "hrz/common/proj.h"

namespace hrz
{

int32_t compute_level_offset(const hrz_proto::TilingSchemeParams& tiling_scheme)
{
    // The level 0 of rasters with local tiling schemes declared through the API
    // is the most zoomed-in at which the whole image fits on one tile.
    // Contrarily, the level 0 of rasters with local tiling schemes that come from
    // a single image raster is the most zoomed-in at which the whole image fits
    // in a single pixel.
    // The level offsets enables the computations to be the same for the two cases.
    //
    // Internally, computations are made using the second convention (level 0 is a
    // single pixel image). At the end of the reprojection process, the offset
    // returned by this function is applied to the levels that have been computed
    // by the job in order to translate them to the convention of the source data,
    // which is what its raster provider expects.
    // For example, if the raster has one tile at level 0, of size 256x256 pixels,
    // the offset is -8: Internal level 0 is 1x1 pixel, level 1 is 2x2, etc., so
    // level 8 is 256x256. This level corresponds to the level 0 of the source raster
    // therefore adding -8 translates internal levels to source levels.
    //
    // The level offset in the local tiling model is reversed, in order to be exposed
    // as a external-to-internal conversion, which is more understandable by the user.
    switch (tiling_scheme.type())
    {
        case hrz_proto::TilingSchemeType::GLOBAL:
            return -std::ceil(
                std::log2(
                    tiling_scheme.global_tiling().tile_size()
                    * std::max(
                        tiling_scheme.global_tiling().level_zero_tile_count_x(),
                        tiling_scheme.global_tiling().level_zero_tile_count_y())));
        case hrz_proto::TilingSchemeType::LOCAL:
            if (tiling_scheme.local_tiling().override_level_offset())
            {
                return -tiling_scheme.local_tiling().level_offset();
            }
            else
            {
                return -std::ceil(std::log2(tiling_scheme.local_tiling().tile_size()));
            }
        case hrz_proto::TilingSchemeType::UNKNOWN: return 0;
        default: assert(false && "Unhandled tiling scheme type.");
    }

    return 0;
}

ImageTilingInfo compute_image_tiling_info(
    const hrz::planet::TiledRasterGeometry& raster_params,
    const pl_Crs* image_projection_crs)
{
    ImageTilingInfo info;

    if (raster_params.tiling_scheme.type() == hrz_proto::GLOBAL)
    {
        auto& tiling_scheme = raster_params.tiling_scheme.global_tiling();

        info.domain_bounds = raster_params.projection_bounds;
        if (info.domain_bounds.min == lm::dvec2(0) && info.domain_bounds.max == lm::dvec2(0))
        {
            // Use pre-defined bounds for EPSG:4326 and EPSG:3857.
            if (pl_are_crs_equal(image_projection_crs, &hrz_proj::lonlat_deg))
            {
                info.domain_bounds = hrz_proj::wgs84_bounds;
            }
            else if (pl_are_crs_equal(image_projection_crs, &hrz_proj::wmerc))
            {
                info.domain_bounds = hrz_proj::web_mercator_bounds;
            }
        }
        assert(info.domain_bounds.min.x < info.domain_bounds.max.x);
        assert(info.domain_bounds.min.y < info.domain_bounds.max.y);

        info.domain_bounds_size = lm::size(info.domain_bounds);

        info.min_lod = tiling_scheme.min_level();
        info.max_lod = tiling_scheme.max_level();
        info.provider_tile_pixel_size = tiling_scheme.tile_size();
        info.level_zero_tile_count_x = tiling_scheme.level_zero_tile_count_x();
        info.level_zero_tile_count_y = tiling_scheme.level_zero_tile_count_y();

        // Having more than one tile at level 0 on any axis is equivalent
        // to offsetting the levels.
        info.lod_offset = compute_level_offset(raster_params.tiling_scheme);

        info.domain_pixel_size = {
            info.level_zero_tile_count_x * ((uint64_t)1 << info.max_lod)
                * info.provider_tile_pixel_size,
            info.level_zero_tile_count_y * ((uint64_t)1 << info.max_lod)
                * info.provider_tile_pixel_size
        };

        info.max_mipmap_level = std::ceil(std::log2(lm::maxelem(info.domain_pixel_size)));

        info.tiles_always_full_size =
            tiling_scheme.border_tile_aspect() == hrz_proto::BorderTileAspect::FULL_SIZED;
        info.tiling_origin = hrz_proto::TilingOrigin::TOP_ORIGIN;
    }
    else if (raster_params.tiling_scheme.type() == hrz_proto::LOCAL)
    {
        auto& tiling_scheme = raster_params.tiling_scheme.local_tiling();

        info.lod_offset = compute_level_offset(raster_params.tiling_scheme);
        info.min_lod = tiling_scheme.has_min_level() ? tiling_scheme.min_level() : 0;
        info.max_lod = tiling_scheme.has_max_level() ? tiling_scheme.max_level() : 30;
        info.provider_tile_pixel_size = tiling_scheme.tile_size();
        info.level_zero_tile_count_x = 1;
        info.level_zero_tile_count_y = 1;

        info.domain_bounds = lm::dbbox2(
            {raster_params.projection_bounds.min.x, raster_params.projection_bounds.min.y},
            {raster_params.projection_bounds.max.x, raster_params.projection_bounds.max.y});

        info.domain_bounds_size = lm::size(info.domain_bounds);

        info.domain_pixel_size =
            lm::ulvec2(tiling_scheme.full_image_width(), tiling_scheme.full_image_height());

        info.max_mipmap_level = std::ceil(std::log2(lm::maxelem(info.domain_pixel_size)));

        info.tiles_always_full_size =
            tiling_scheme.border_tile_aspect() == hrz_proto::BorderTileAspect::FULL_SIZED;
        info.tiling_origin = tiling_scheme.tiling_origin();
    }
    else if (raster_params.tiling_scheme.type() == hrz_proto::UNKNOWN)
    {
        // Deprecated
        info.lod_offset = 0;
        info.min_lod = 0;
        info.max_lod = 0;
        info.provider_tile_pixel_size = 256;
        info.level_zero_tile_count_x = 1;
        info.level_zero_tile_count_y = 1;
        info.domain_bounds = lm::dbbox2::invalid();
        info.domain_bounds_size = {0.0, 0.0};
        info.domain_pixel_size = {0, 0};
        info.max_mipmap_level = 0;
        info.tiles_always_full_size = false;
        info.tiling_origin = hrz_proto::TilingOrigin::TOP_ORIGIN;
    }
    else
    {
        assert(false && "Unhandled case");
    }

    if (raster_params.bounds.min == lm::dvec2(0) && raster_params.bounds.max == lm::dvec2(0))
    {
        info.raster_bounds = info.domain_bounds;
    }
    else
    {
        info.raster_bounds = lm::intersection(raster_params.bounds, info.domain_bounds);
    }

    lm::dbbox2 image_pixel_bounds = lm::dbbox2(
        lm::dvec2(
            ((info.raster_bounds.min - info.domain_bounds.min) / info.domain_bounds_size)
            * info.domain_pixel_size),
        lm::dvec2(
            ((info.raster_bounds.max - info.domain_bounds.min) / info.domain_bounds_size)
            * info.domain_pixel_size));

    // Units in the projected space are counted up on the y-axis, but
    // pixels are counted down, hence the inversion.
    image_pixel_bounds.min.y = info.domain_pixel_size.y - image_pixel_bounds.min.y;
    image_pixel_bounds.max.y = info.domain_pixel_size.y - image_pixel_bounds.max.y;
    std::swap(image_pixel_bounds.min.y, image_pixel_bounds.max.y);

    info.image_pixel_bounds = lm::ilbbox2(
        lm::ilvec2(std::round(image_pixel_bounds.min.x), std::round(image_pixel_bounds.min.y)),
        lm::ilvec2(std::round(image_pixel_bounds.max.x), std::round(image_pixel_bounds.max.y)));

    info.image_pixel_size = lm::ulvec2(lm::size(info.image_pixel_bounds));

    info.min_available_mipmap_level =
        std::max(0, info.max_mipmap_level + info.lod_offset - (int)info.max_lod);
    info.max_available_mipmap_level = std::min(
        info.max_mipmap_level, info.max_mipmap_level + info.lod_offset - (int)info.min_lod);

    return info;
}

std::optional<TileCoordsAndUv> proj_pos_to_tile_uv(
    const lm::dvec2& proj_pos,
    uint8_t lod,
    const ImageTilingInfo& tiling_info)
{
    if (!lm::contains(tiling_info.raster_bounds, proj_pos))
    {
        return std::nullopt;
    }

    uint32_t mipmap_level = lod - tiling_info.lod_offset;

    // Size of the tiles at the coords level, expressed in pixels of the max level.
    uint64_t level_tile_pixel_size = tiling_info.provider_tile_pixel_size
        * ((uint64_t)1 << (tiling_info.max_mipmap_level - mipmap_level));

    // Width of the last column of tiles.
    // Height of the first or last row of tiles, depending on the TilingOrigin.
    // This is the actual size of the tiles, or of their content area, depending on the
    // BorderTileAspect.
    lm::ulvec2 small_tile_pixel_size = {
        (uint64_t)tiling_info.domain_pixel_size.x % level_tile_pixel_size,
        (uint64_t)tiling_info.domain_pixel_size.y % level_tile_pixel_size
    };
    if (small_tile_pixel_size.x == 0) small_tile_pixel_size.x = level_tile_pixel_size;
    if (small_tile_pixel_size.y == 0) small_tile_pixel_size.y = level_tile_pixel_size;

    // In (O,1)x(0,1), over the whole domain.
    lm::dvec2 pos_domain_rel =
        (proj_pos - tiling_info.domain_bounds.min) / tiling_info.domain_bounds_size;

    // Tile coords and UV grow from top to bottom. Domain coordinates grow from bottom to top.
    // The y coordinate must be inverted to switch from one convention to the other.
    pos_domain_rel.y = 1.0 - pos_domain_rel.y;

    // Coordinates in domain pixels of the position.
    lm::dvec2 pos_pixels = pos_domain_rel * tiling_info.domain_pixel_size;

    bool tile_is_on_small_column =
        tiling_info.domain_pixel_size.x - pos_pixels.x <= small_tile_pixel_size.x;
    bool tile_is_on_small_line =
        (tiling_info.tiling_origin == hrz_proto::TilingOrigin::TOP_ORIGIN
         && tiling_info.domain_pixel_size.y - pos_pixels.y <= small_tile_pixel_size.y)
        || (tiling_info.tiling_origin == hrz_proto::TilingOrigin::BOTTOM_ORIGIN
            && pos_pixels.y < small_tile_pixel_size.y);

    lm::ulvec2 tile_pixel_size = {
        !tiling_info.tiles_always_full_size && tile_is_on_small_column ? small_tile_pixel_size.x
                                                                       : level_tile_pixel_size,
        !tiling_info.tiles_always_full_size && tile_is_on_small_line ? small_tile_pixel_size.y
                                                                     : level_tile_pixel_size,
    };
    lm::dvec2 tile_to_full_tile_pixel_size_ratio = {
        level_tile_pixel_size / (double)tile_pixel_size.x,
        level_tile_pixel_size / (double)tile_pixel_size.y
    };

    // Coordinates in domain pixels of the top-left corner of the tile.
    lm::dvec2 tile_origin_pixels;

    double div_x = pos_pixels.x / level_tile_pixel_size;
    uint32_t tile_x = div_x;
    tile_origin_pixels.x = tile_x * level_tile_pixel_size;
    double u = (div_x - std::floor(div_x)) * tile_to_full_tile_pixel_size_ratio.x;

    uint32_t tile_y = 0;
    double v = 0.0;
    if (tiling_info.tiling_origin == hrz_proto::TilingOrigin::TOP_ORIGIN)
    {
        double div_y = pos_pixels.y / level_tile_pixel_size;
        tile_y = div_y;
        tile_origin_pixels.y = tile_y * level_tile_pixel_size;
        v = (div_y - std::floor(div_y)) * tile_to_full_tile_pixel_size_ratio.y;
    }
    else if (tiling_info.tiling_origin == hrz_proto::TilingOrigin::BOTTOM_ORIGIN)
    {
        if (!tiling_info.tiles_always_full_size && pos_pixels.y < small_tile_pixel_size.y)
        {
            tile_y = 0;
            tile_origin_pixels.y = 0;
            v = pos_pixels.y / small_tile_pixel_size.y;
        }
        else
        {
            double div_y = (pos_pixels.y + (level_tile_pixel_size - small_tile_pixel_size.y))
                / level_tile_pixel_size;
            tile_y = div_y;
            tile_origin_pixels.y = (tile_y - 1) * level_tile_pixel_size + small_tile_pixel_size.y;
            v = (div_y - std::floor(div_y)) * tile_to_full_tile_pixel_size_ratio.y;
        }
    }
    else
    {
        assert(false && "Unhandled case");
    }

    lm::dvec2 uv = {u, v};
    lm::ulvec2 image_tile_pixel_size = tile_pixel_size;

    if (!tiling_info.tiles_always_full_size)
    {
        // Restrict to the area covered by the image.

        lm::dbbox2 image_pixel_bounds = lm::dbbox2(tiling_info.image_pixel_bounds);

        lm::dbbox2 tile_bbox_pixels =
            lm::dbbox2(tile_origin_pixels, tile_origin_pixels + tile_pixel_size);
        lm::dbbox2 image_tile_bbox_pixels = lm::intersection(tile_bbox_pixels, image_pixel_bounds);

        uv = ((tile_origin_pixels + tile_pixel_size * uv) - image_tile_bbox_pixels.min)
            / lm::size(image_tile_bbox_pixels);
        image_tile_pixel_size = lm::ulvec2(lm::size(image_tile_bbox_pixels));
    }

    // So far we have been working with pixel sizes in the pixel domain of the most detailed
    // level. But here we compute the physical size in pixels of the tile.
    double actual_pixel_size_factor =
        tiling_info.provider_tile_pixel_size / (double)level_tile_pixel_size;
    lm::uvec2 actual_image_tile_pixel_size =
        lm::uvec2(image_tile_pixel_size * actual_pixel_size_factor);

    return {{{tile_x, tile_y, lod}, uv, actual_image_tile_pixel_size}};
}

lm::dvec2 tile_uv_to_proj_pos(
    const TileCoords& tile_coords,
    const lm::dvec2& uv,
    const ImageTilingInfo& tiling_info)
{
    uint32_t mipmap_level = tile_coords.lod - tiling_info.lod_offset;

    // Size of the tiles at the coords level, expressed in pixels of the max level.
    uint64_t level_tile_pixel_size = tiling_info.provider_tile_pixel_size
        * ((uint64_t)1 << (tiling_info.max_mipmap_level - mipmap_level));

    // Width of the last column of tiles.
    // Height of the first or last row of tiles, depending on the TilingOrigin.
    // This is the actual size of the tiles, or of their content area, depending on the
    // BorderTileAspect.
    lm::ulvec2 small_tile_pixel_size = {
        (uint64_t)tiling_info.domain_pixel_size.x % level_tile_pixel_size,
        (uint64_t)tiling_info.domain_pixel_size.y % level_tile_pixel_size
    };
    if (small_tile_pixel_size.x == 0) small_tile_pixel_size.x = level_tile_pixel_size;
    if (small_tile_pixel_size.y == 0) small_tile_pixel_size.y = level_tile_pixel_size;

    // Coordinates of the top-left corner of the tile, in (O,1)x(0,1), over the whole domain.
    double tile_domain_rel_x =
        (tile_coords.x * level_tile_pixel_size) / (double)tiling_info.domain_pixel_size.x;
    double tile_domain_rel_y = 0.0;
    if (tiling_info.tiling_origin == hrz_proto::TilingOrigin::TOP_ORIGIN)
    {
        tile_domain_rel_y =
            (tile_coords.y * level_tile_pixel_size) / (double)tiling_info.domain_pixel_size.y;
    }
    else if (tiling_info.tiling_origin == hrz_proto::TilingOrigin::BOTTOM_ORIGIN)
    {
        if (!tiling_info.tiles_always_full_size && tile_coords.y == 0)
        {
            tile_domain_rel_y = 0.0;
        }
        else
        {
            tile_domain_rel_y = ((double)tile_coords.y * level_tile_pixel_size
                                 - (level_tile_pixel_size - small_tile_pixel_size.y))
                / (double)tiling_info.domain_pixel_size.y;
        }
    }
    else
    {
        assert(false && "Unhandled case");
    }
    lm::dvec2 tile_domain_rel = {tile_domain_rel_x, tile_domain_rel_y};

    // Coordinates in domain pixels of the top-left corner of the tile.
    lm::dvec2 tile_origin_pixels = tile_domain_rel * lm::dvec2(tiling_info.domain_pixel_size);

    bool tile_is_on_small_column =
        tiling_info.domain_pixel_size.x - tile_coords.x * level_tile_pixel_size
        <= level_tile_pixel_size;
    bool tile_is_on_small_line =
        (tiling_info.tiling_origin == hrz_proto::TilingOrigin::TOP_ORIGIN
         && tiling_info.domain_pixel_size.y - tile_coords.y * level_tile_pixel_size
             <= level_tile_pixel_size)
        || (tiling_info.tiling_origin == hrz_proto::TilingOrigin::BOTTOM_ORIGIN
            && tile_coords.y == 0);

    lm::ulvec2 tile_pixel_size = {
        !tiling_info.tiles_always_full_size && tile_is_on_small_column ? small_tile_pixel_size.x
                                                                       : level_tile_pixel_size,
        !tiling_info.tiles_always_full_size && tile_is_on_small_line ? small_tile_pixel_size.y
                                                                     : level_tile_pixel_size
    };

    lm::dvec2 tile_uv = uv;

    if (!tiling_info.tiles_always_full_size)
    {
        // Restrict to the area covered by the image.

        lm::dbbox2 image_pixel_bounds = lm::dbbox2(tiling_info.image_pixel_bounds);

        lm::dbbox2 tile_bbox_pixels =
            lm::dbbox2(tile_origin_pixels, tile_origin_pixels + tile_pixel_size);
        lm::dbbox2 image_tile_bbox_pixels = lm::intersection(tile_bbox_pixels, image_pixel_bounds);

        tile_uv = ((image_tile_bbox_pixels.min + lm::size(image_tile_bbox_pixels) * uv)
                   - tile_origin_pixels)
            / tile_pixel_size;
    }

    // In (O,1)x(0,1), over the whole domain.
    lm::dvec2 uv_domain_rel =
        tile_uv * (tile_pixel_size / lm::dvec2(tiling_info.domain_pixel_size));
    lm::dvec2 pos_domain_rel = tile_domain_rel + uv_domain_rel;

    // Tile coords and UV grow from top to bottom. Domain coordinates grow from bottom to top.
    // The y coordinate must be inverted to switch from one convention to the other.
    pos_domain_rel.y = 1.0 - pos_domain_rel.y;

    return tiling_info.domain_bounds.min + pos_domain_rel * tiling_info.domain_bounds_size;
}

} // namespace hrz
