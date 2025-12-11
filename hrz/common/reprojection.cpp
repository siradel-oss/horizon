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
            return -std::ceil(std::log2(
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
        case hrz_proto::TilingSchemeType::UNTILED: return 0;
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
                * info.provider_tile_pixel_size};

        info.max_mipmap_level = std::ceil(std::log2(lm::maxelem(info.domain_pixel_size)));

        info.tiles_always_full_size =
            tiling_scheme.border_tile_aspect() == hrz_proto::BorderTileAspect::FULL_SIZED;
        info.tiling_origin = hrz_proto::TilingOrigin::TOP_ORIGIN;
    }
    else if (raster_params.tiling_scheme.type() == hrz_proto::LOCAL)
    {
        auto& tiling_scheme = raster_params.tiling_scheme.local_tiling();

        info.lod_offset = compute_level_offset(raster_params.tiling_scheme);
        info.min_lod = tiling_scheme.min_level();
        info.max_lod = tiling_scheme.max_level();
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
    else
    {
        assert(false && "Unhandled case");
    }

    lm::dbbox2 image_bounds = raster_params.bounds;
    if (image_bounds.min == lm::dvec2(0) && image_bounds.max == lm::dvec2(0))
    {
        image_bounds = info.domain_bounds;
    }

    lm::ilbbox2 image_pixel_bounds = lm::ilbbox2(
        lm::ilvec2(
            ((image_bounds.min - info.domain_bounds.min) / info.domain_bounds_size)
            * info.domain_pixel_size),
        lm::ilvec2(
            ((image_bounds.max - info.domain_bounds.min) / info.domain_bounds_size)
            * info.domain_pixel_size));

    // Units in the projected space are counted up on the y-axis, but
    // pixels are counted down, hence the inversion.
    image_pixel_bounds.min.y = info.domain_pixel_size.y - image_pixel_bounds.min.y;
    image_pixel_bounds.max.y = info.domain_pixel_size.y - image_pixel_bounds.max.y;
    std::swap(image_pixel_bounds.min.y, image_pixel_bounds.max.y);

    info.image_pixel_bounds = image_pixel_bounds;

    info.image_pixel_size = lm::ulvec2(lm::size(info.image_pixel_bounds));

    info.min_available_mipmap_level =
        std::max(0, info.max_mipmap_level + info.lod_offset - (int)info.max_lod);
    info.max_available_mipmap_level = std::min(
        info.max_mipmap_level, info.max_mipmap_level + info.lod_offset - (int)info.min_lod);

    if (raster_params.bounds.min == lm::dvec2(0) && raster_params.bounds.max == lm::dvec2(0))
    {
        info.raster_bounds = info.domain_bounds;
    }
    else
    {
        info.raster_bounds = raster_params.bounds;
    }

    return info;
}
} // namespace hrz
