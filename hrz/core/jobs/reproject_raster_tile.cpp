#include "hrz/common/crs_database.h"
#include "hrz/common/geo.h"
#include "hrz/common/planet.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/proto_maths.h"
#include "hrz/common/reprojection.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"

#include <optional>

using namespace hrz;

namespace
{
static const int MERCATOR_BORDER_SIZE = ATLAS_TILE_BORDER_SIZE == 0
    ? 0
    : std::max(MERCATOR_TILE_SIZE / (ATLAS_TILE_SIZE - 2 * ATLAS_TILE_BORDER_SIZE), 1);

static constexpr size_t MaxLevelDifferenceForDownsampling = 2;
static constexpr size_t MaxRasterTilesPerPlanetTile = 16;

// This is pretty much a copy of SignedTileCoords, except with
// signed x and y values.
struct SignedTileCoords
{
    int32_t x, y;
    uint8_t lod;

    constexpr bool operator==(const SignedTileCoords& t) const = default;

    SignedTileCoords to_parent_tile() const
    {
        if (lod == 0) return {0, 0, 0};

        return {x / 2, y / 2, (uint8_t)(lod - 1)};
    }
};

// Tile infos for grid computation
struct TileInfos
{
    int32_t x, y;
    uint8_t lod;

    uint32_t width, height;
    double bounds_x0, bounds_y0;
    double bounds_width, bounds_height;
};
} // namespace

namespace std
{
template<>
struct hash<SignedTileCoords>
{
    size_t operator()(const SignedTileCoords& v) const noexcept
    {
        auto h1(std::hash<int32_t>{}(v.x));
        auto h2(std::hash<int32_t>{}(v.y));
        auto h3(std::hash<uint8_t>{}(v.lod));
        return hrz::hash_mix(hrz::hash_mix(h1, h2), h3);
    }
};
} // namespace std

namespace hrz_jobs::reproject_raster_tile
{
void compute_tiled_mercator_reprojection(
    const hrz::planet::RasterTileReprojParams& params,
    hrz::planet::ReprojectedTiles& response)
{
    HRZ_SCOPED_SAMPLE("compute tiled mercator reprojection");

    const auto& raster_geometry = params.raster_geometry;

    // The following code makes these assumptions.
    assert(MERCATOR_TILE_SIZE == ATLAS_TILE_SIZE - 2 * ATLAS_TILE_BORDER_SIZE);
    assert(raster_geometry.tiling_scheme.has_global_tiling());
    assert(raster_geometry.tiling_scheme.global_tiling().tile_size() == MERCATOR_TILE_SIZE);
    assert(raster_geometry.tiling_scheme.global_tiling().level_zero_tile_count_x() == 1);
    assert(raster_geometry.tiling_scheme.global_tiling().level_zero_tile_count_y() == 1);
    assert(
        raster_geometry.tiling_scheme.global_tiling().border_tile_aspect()
        == hrz_proto::BorderTileAspect::FULL_SIZED);

    // Don't know why it happens but it does...
    if (params.tile_coords.lod >= CLIPMAP_LOD_COUNT)
    {
        return;
    }

    const auto raster_min_lod = (uint8_t)raster_geometry.tiling_scheme.global_tiling().min_level();
    const auto raster_max_lod = (uint8_t)raster_geometry.tiling_scheme.global_tiling().max_level();

    if (raster_min_lod > raster_max_lod) return;

    if (params.tile_coords.lod + MaxLevelDifferenceForDownsampling < raster_min_lod)
    {
        // Too many tiles would be selected, so we bail out.
        return;
    }

    const int32_t planet_tile_lod_from_pyramid_bottom =
        CLIPMAP_LOD_COUNT - params.tile_coords.lod - 1;
    const int64_t planet_pixel_size = 1 << planet_tile_lod_from_pyramid_bottom;
    const int64_t planet_tile_pixel_size = planet_pixel_size * MERCATOR_TILE_SIZE;
    const int64_t planet_border_pixel_size = planet_pixel_size * ATLAS_TILE_BORDER_SIZE;

    const int64_t planet_tile_x0_pixel =
        (int64_t)params.tile_coords.x * planet_tile_pixel_size - planet_border_pixel_size;
    const int64_t planet_tile_y0_pixel =
        (int64_t)params.tile_coords.y * planet_tile_pixel_size - planet_border_pixel_size;
    const int64_t planet_tile_x1_pixel =
        planet_tile_x0_pixel + planet_tile_pixel_size + 2 * planet_border_pixel_size - 1;
    const int64_t planet_tile_y1_pixel =
        planet_tile_y0_pixel + planet_tile_pixel_size + 2 * planet_border_pixel_size - 1;

    const uint32_t raster_lod =
        std::max(raster_min_lod, std::min(raster_max_lod, params.tile_coords.lod));
    const uint32_t raster_tile_lod_from_pyramid_bottom = CLIPMAP_LOD_COUNT - raster_lod - 1;
    const int64_t raster_tile_pixel_size = (int64_t)(MERCATOR_TILE_SIZE)
        << raster_tile_lod_from_pyramid_bottom;

    std::optional<lm::dbbox2> raster_bounds = std::nullopt;
    {
        const auto& geometry_bounds = params.raster_geometry.bounds;
        const bool geometry_bounds_set =
            !(geometry_bounds.min == lm::dvec2(0) && geometry_bounds.max == lm::dvec2(0));

        const auto& display_bounds = params.raster_display_bounds;
        const bool display_bounds_set =
            !(geometry_bounds.min == lm::dvec2(-hrz::HALF_MERCATOR_RANGE)
              && geometry_bounds.max == lm::dvec2(hrz::HALF_MERCATOR_RANGE));

        if (geometry_bounds_set && display_bounds_set)
        {
            raster_bounds = {lm::intersection(geometry_bounds, display_bounds)};
        }
        else if (geometry_bounds_set)
        {
            raster_bounds = geometry_bounds;
        }
        else if (display_bounds_set)
        {
            raster_bounds = display_bounds;
        }
    }

    hrz::flat_hash_set<hrz::TileCoords> web_mercator_tiles;
    if (raster_lod == params.tile_coords.lod && !raster_bounds.has_value())
    {
        web_mercator_tiles.insert(params.tile_coords);
    }

    const auto x0 =
        (int64_t)std::floor((double)planet_tile_x0_pixel / (double)raster_tile_pixel_size);
    const auto x1 =
        (int64_t)std::floor((double)planet_tile_x1_pixel / (double)raster_tile_pixel_size);
    const auto y0 =
        (int64_t)std::floor((double)planet_tile_y0_pixel / (double)raster_tile_pixel_size);
    const auto y1 =
        (int64_t)std::floor((double)planet_tile_y1_pixel / (double)raster_tile_pixel_size);

    for (int64_t y = y0; y <= y1; ++y)
    {
        for (int64_t x = x0; x <= x1; ++x)
        {
            SignedTileCoords mercator_tile = {(int32_t)x, (int32_t)y, (uint8_t)raster_lod};

            // Handle tiles that wrap around the earth
            // For X we simply wrap around.
            int32_t level_size = 1 << mercator_tile.lod;

            if (mercator_tile.x < 0) mercator_tile.x = mercator_tile.x + level_size;
            if (mercator_tile.x >= level_size) mercator_tile.x = mercator_tile.x - level_size;

            // For Y we don't need special case. We will have a border with mirrored pixels.
            if (mercator_tile.y < 0)
            {
                continue;
            }

            if (mercator_tile.y >= level_size)
            {
                continue;
            }

            const bool tile_within_zoom_levels =
                raster_lod >= raster_min_lod && raster_lod <= raster_max_lod;

            hrz::TileCoords unsigned_mercator_tile(
                (uint32_t)mercator_tile.x, (uint32_t)mercator_tile.y, mercator_tile.lod);

            lm::dbbox2 tile_bounds = hrz::mercator_tile_bbox_meters(unsigned_mercator_tile);

            if (tile_within_zoom_levels
                && (!raster_bounds.has_value()
                    || lm::intersect(tile_bounds, raster_bounds.value())))
            {
                web_mercator_tiles.insert(unsigned_mercator_tile);
            }
        }
    }

    for (const auto& mercator_tile : web_mercator_tiles)
    {
        // Compute the clipping rectangle in UV coordinates. (0, 0)-(1, 1) means
        // that the whole tile is displayed. This is used to deal with tiles
        // that partially intersect the displayable bounds of the raster.
        lm::bbox2 uv_clipping;
        if (!raster_bounds.has_value())
        {
            uv_clipping = lm::bbox2{{0, 0}, {1, 1}};
        }
        else
        {
            lm::dbbox2 tile_bounds = hrz::mercator_tile_bbox_meters(mercator_tile);
            lm::dbbox2 clipped = lm::intersection(tile_bounds, raster_bounds.value());
            lm::dvec2 tile_bounds_size = lm::size(tile_bounds);

            uv_clipping.min = lm::vec2((clipped.min - tile_bounds.min) / tile_bounds_size);
            uv_clipping.max = lm::vec2((clipped.max - tile_bounds.min) / tile_bounds_size);

            // Reverse the Y axis
            std::swap(uv_clipping.min.y, uv_clipping.max.y);
            uv_clipping.min.y = 1.0f - uv_clipping.min.y;
            uv_clipping.max.y = 1.0f - uv_clipping.max.y;
        }

        response.tiles.emplace_back();
        auto& tile = response.tiles.back();
        tile.coords = mercator_tile;
        tile.uv_clip = uv_clipping;
    }
}

lm::dbbox2 bounds_from_points(std::span<const lm::dvec2> points)
{
    lm::dbbox2 bounds(points[0], points[0]);
    for (const lm::dvec2& pt : points)
    {
        bounds.min = lm::min(bounds.min, pt);
        bounds.max = lm::max(bounds.max, pt);
    }
    return bounds;
}

lm::ilbbox2 compute_tile_pixel_bounds(SignedTileCoords coords, const ImageTilingInfo& info)
{
    lm::ilbbox2 tile_bounds;

    auto mipmap_level = info.max_mipmap_level - coords.lod;
    auto tile_pixel_size = info.provider_tile_pixel_size * ((uint64_t)1 << mipmap_level);

    tile_bounds.min.x = coords.x * tile_pixel_size;
    tile_bounds.max.x = (coords.x + 1) * tile_pixel_size;

    if (info.tiling_origin == hrz_proto::TilingOrigin::TOP_ORIGIN)
    {
        tile_bounds.min.y = coords.y * tile_pixel_size;
        tile_bounds.max.y = (coords.y + 1) * tile_pixel_size;
    }
    else if (info.tiling_origin == hrz_proto::TilingOrigin::BOTTOM_ORIGIN)
    {
        tile_bounds.min.y = info.domain_pixel_size.y - (coords.y + 1) * tile_pixel_size;
        tile_bounds.max.y = info.domain_pixel_size.y - coords.y * tile_pixel_size;
    }
    else
    {
        assert(false && "Unhandled case");
    }

    return tile_bounds;
}

void select_tiled_image_tiles(
    const hrz::planet::RasterTileReprojParams& params,
    const pl_Crs* image_projection_crs,
    const ImageTilingInfo& info,
    hrz::flat_hash_set<SignedTileCoords>& selected_tiles)
{
    HRZ_SCOPED_SAMPLE("select tiled image tiles");

    // Compute reprojected planet tile coords in pixels in its zoom level.

    uint32_t tile_count_at_lod = 1 << params.tile_coords.lod;
    uint32_t pixel_count_at_lod = tile_count_at_lod * MERCATOR_TILE_SIZE;

    int32_t quad_size = params.quad_size;
    int32_t quad_count = MERCATOR_TILE_SIZE / quad_size + 2;
    int32_t line_point_count = quad_count + 1;
    int32_t grid_point_count = line_point_count * line_point_count;

    int64_t tile_x0_pixel = (int64_t)params.tile_coords.x * MERCATOR_TILE_SIZE;
    int64_t tile_y0_pixel = (int64_t)params.tile_coords.y * MERCATOR_TILE_SIZE;

    // Make a grid on the reprojected planet tile.
    // The grid is composed of mostly regularly spaced points, organised in quads.
    // The size of the quads (in pixels) is given through the job parameters.
    // The grid always has points on the pixels bordering the tile, both inside and
    // outside, on all sides. The pixels on the outside help with determining the
    // mipmap level for images that intersect the tile, but only barely. With the
    // border points the chances of having enough coordinates to be able to compute
    // a mipmap level are much higher. (If it is still not sufficient, the lowest
    // LOD is used.)
    // The grid could be clipped by the display bounds, at the added cost of even more
    // complexity. So because the display bounds are not taken into account, some
    // tiles can be selected, although they won't be displayed later on.
    // (If however, the planet tile wasn't inside the raster display bounds at all,
    // this job shouldn't have been created in the first place.)

    // Compute the Mercator coords for each point on the grid.

    std::vector<lm::dvec3> grid_coords(grid_point_count);
    std::vector<lm::ivec2> in_tile_pixel_positions(grid_point_count);

    auto point_index_to_pixel_pos = [&](int point_index, int64_t tile_origin_pixel)
    {
        if (point_index == 0)
        {
            // First interior border point
            return tile_origin_pixel - MERCATOR_BORDER_SIZE;
        }
        else if (point_index == line_point_count - 2)
        {
            // Second exterior border point
            return tile_origin_pixel + MERCATOR_TILE_SIZE - 1;
        }
        else if (point_index == line_point_count - 1)
        {
            // Second exterior border point
            return tile_origin_pixel + MERCATOR_TILE_SIZE + MERCATOR_BORDER_SIZE;
        }
        else
        {
            // Interior points, including first interior border point
            return tile_origin_pixel + (point_index - 1) * quad_size;
        }
    };

    // (0.5, 0.5) is added to the pixel positions in order to get the projection
    // position of their center.
    for (int y_quad = 0; y_quad < line_point_count; y_quad++)
    {
        // The y-axis is inverted because the tiles (and their pixels) are counted southwards,
        // but the Mercator projection counts metres northwards.
        int64_t y_pixel = point_index_to_pixel_pos(y_quad, tile_y0_pixel);
        double y_meter = ((1 - ((double)y_pixel + 0.5) / (double)pixel_count_at_lod) * 2 - 1)
            * HALF_MERCATOR_RANGE;

        for (int x_quad = 0; x_quad < line_point_count; x_quad++)
        {
            int64_t x_pixel = point_index_to_pixel_pos(x_quad, tile_x0_pixel);
            double x_meter = ((((double)x_pixel + 0.5) / (double)pixel_count_at_lod) * 2 - 1)
                * HALF_MERCATOR_RANGE;

            // In some cases the coordinates can be outside the bounds on the x-axis,
            // due to the margins.
            // Add (or subtract) whole planet revolutions to put them back in bounds.
            while (x_meter < -HALF_MERCATOR_RANGE)
            {
                x_meter += MERCATOR_RANGE;
            }
            while (x_meter > HALF_MERCATOR_RANGE)
            {
                x_meter -= MERCATOR_RANGE;
            }

            size_t index = y_quad * line_point_count + x_quad;
            grid_coords[index] = lm::dvec3(x_meter, y_meter, 0);

            in_tile_pixel_positions[index] =
                lm::ivec2((int)(x_pixel - tile_x0_pixel), (int)(y_pixel - tile_y0_pixel));
        }
    }

    // Transform the grid points from web Mercator to the projection of the tiled image.
    {
        HRZ_SCOPED_SAMPLE("web mercator to image projection");

        pl_Transform web_mercator_to_image_transform;
        pl_bake_transform(&hrz_proj::wmerc, image_projection_crs, &web_mercator_to_image_transform);
        pl_transform_in_place_canonical(
            &web_mercator_to_image_transform, grid_point_count, &grid_coords[0].x);
    }

    // Compute the coordinates of the grid points (in pixels) in the tiled image.

    std::vector<lm::dvec2> pixel_grid_coords(grid_point_count);

    // Store whether the grid points are inside the tiled image bounds.
    std::vector<bool> point_in_bounds(grid_point_count);
    unsigned int point_in_bounds_count = 0;

    lm::dbbox2 grid_bbox_pixel = lm::dbbox2::invalid();
    lm::dbbox2 grid_bbox_units = lm::dbbox2::invalid();

    for (unsigned int i = 0; i < (unsigned int)grid_point_count; i++)
    {
        lm::dvec2 domain_coords(
            (grid_coords[i].x - info.domain_bounds.min.x) / info.domain_bounds_size.x,
            (info.domain_bounds.max.y - grid_coords[i].y) / info.domain_bounds_size.y);

        lm::dvec2 domain_coords_pixel(domain_coords * info.domain_pixel_size);

        grid_bbox_pixel = lm::expand(grid_bbox_pixel, domain_coords_pixel);
        grid_bbox_units = lm::expand(grid_bbox_units, grid_coords[i].xy);

        if (lm::contains(info.image_pixel_bounds, domain_coords_pixel))
        {
            point_in_bounds[i] = true;
            point_in_bounds_count += 1;
        }
        else
        {
            point_in_bounds[i] = false;
        }

        pixel_grid_coords[i] = domain_coords_pixel;
    }

    // Stop here if the tiled image and the reprojected tile have an empty intersection.
    if (grid_bbox_pixel.min.x >= (double)info.domain_pixel_size.x
        || grid_bbox_pixel.min.y >= (double)info.domain_pixel_size.y || grid_bbox_pixel.max.x <= 0
        || grid_bbox_pixel.max.y <= 0)
    {
        return;
    }

    // Stop here if the tile is not in the raster bounds
    if (!lm::intersect(grid_bbox_units, info.raster_bounds))
    {
        return;
    }

    // Compute mipmap levels for the grid points on the image.
    // This is done by computing the distance (in pixels, on the tiled image) between
    // neighbouring points of the grid.

    std::vector<int32_t> mipmap_levels(grid_point_count, -1);

    unsigned int points_with_mipmap_level = 0;

    {
        HRZ_SCOPED_SAMPLE("compute grid mipmap levels");

        for (int y_quad = 0; y_quad < line_point_count; y_quad++)
        {
            for (int x_quad = 0; x_quad < line_point_count; x_quad++)
            {
                unsigned int point_index = y_quad * line_point_count + x_quad;

                // Refer to a neighbour on the right, except for the last row,
                // which must refer to a neighbour on its left.
                unsigned int h_neighbor_index =
                    (x_quad == line_point_count - 1) ? point_index - 1 : point_index + 1;

                // Same for the y-axis.
                unsigned int v_neighbor_index = (y_quad == line_point_count - 1)
                    ? point_index - line_point_count
                    : point_index + line_point_count;

                if (!(point_in_bounds[point_index] && point_in_bounds[h_neighbor_index]
                      && point_in_bounds[v_neighbor_index]))
                {
                    // Not all points of the triplet are in-bounds, cannot determine mipmap
                    // level.

                    // @Todo This could be enhanced by checking the top- and left-neighbours,
                    // if they exist.
                    continue;
                }

                lm::dvec2 point_pixel = pixel_grid_coords[point_index];
                lm::dvec2 h_neighbor_pixel = pixel_grid_coords[h_neighbor_index];
                lm::dvec2 v_neighbor_pixel = pixel_grid_coords[v_neighbor_index];

                double h_pixel_dist = std::abs(
                    in_tile_pixel_positions[point_index].x
                    - in_tile_pixel_positions[h_neighbor_index].x);
                double v_pixel_dist = std::abs(
                    in_tile_pixel_positions[point_index].y
                    - in_tile_pixel_positions[v_neighbor_index].y);

                double h_dist = lm::length(h_neighbor_pixel - point_pixel) / h_pixel_dist;
                double v_dist = lm::length(v_neighbor_pixel - point_pixel) / v_pixel_dist;

                double max_dist = std::max(h_dist, v_dist);
                if (max_dist > 0)
                {
                    int mipmap_level = (int)std::floor(std::log2(max_dist) + 0.5);
                    mipmap_level = std::max(0, std::min(mipmap_level, info.max_mipmap_level));

                    // Don't set the mipmap level when it would select too many raster tiles.
                    if (mipmap_level
                        <= info.max_available_mipmap_level + (int)MaxLevelDifferenceForDownsampling)
                    {
                        mipmap_levels[point_index] = std::max(
                            std::min(mipmap_level, info.max_available_mipmap_level),
                            info.min_available_mipmap_level);

                        points_with_mipmap_level += 1;
                    }
                }
            }
        }
    }

    std::optional<int> approximate_mipmap_level = std::nullopt;
    auto compute_approximate_mipmap_level = [&]()
    {
        // When we couldn't determine any mipmap level, we use the area heuristic
        // to try to find an acceptable mipmap level for the entire tile.

        // To compute the area of the tile, we sum the areas of each quad.

        double tile_area = 0.0;
        for (int y_quad = 0; y_quad < line_point_count - 1; y_quad++)
        {
            for (int x_quad = 0; x_quad < line_point_count - 1; x_quad++)
            {
                lm::dvec2 p0 = pixel_grid_coords[x_quad + y_quad * line_point_count];
                lm::dvec2 p1 = pixel_grid_coords[x_quad + 1 + y_quad * line_point_count];
                lm::dvec2 p2 = pixel_grid_coords[x_quad + (y_quad + 1) * line_point_count];
                lm::dvec2 p3 = pixel_grid_coords[x_quad + 1 + (y_quad + 1) * line_point_count];

                tile_area += lm::length(lm::cross(lm::dvec3(p1 - p0), lm::dvec3(p3 - p0))) / 2;
                tile_area += lm::length(lm::cross(lm::dvec3(p3 - p0), lm::dvec3(p2 - p0))) / 2;
            }
        }

        const double tile_size_log2 = std::log2(MERCATOR_TILE_SIZE);
        int level = (int)std::floor(0.5 * std::log2(tile_area) - tile_size_log2 + 0.5);
        level = std::min(
            std::max(level, info.min_available_mipmap_level), info.max_available_mipmap_level);
        level = std::max(0, level);

        approximate_mipmap_level = std::max(
            std::min(level, info.max_available_mipmap_level), info.min_available_mipmap_level);
    };

    if (points_with_mipmap_level == 0)
    {
        if (!approximate_mipmap_level.has_value())
        {
            compute_approximate_mipmap_level();
            assert(approximate_mipmap_level.has_value());
        }

        for (int y_quad = 0; y_quad < line_point_count; y_quad++)
        {
            for (int x_quad = 0; x_quad < line_point_count; x_quad++)
            {
                unsigned int index = y_quad * line_point_count + x_quad;
                mipmap_levels[index] = approximate_mipmap_level.value();
            }
        }

        // Now, it's still possible that no point are on the image and in this case
        // we won't select any tile, which is not good!
        // We take the intersection of the image bounds and the tile bounds and generate
        // a new grid of points there.
        // This will force the following step to select tiles at the LOD selected above.
        lm::dbbox2 tile_bounds = bounds_from_points(pixel_grid_coords);
        lm::dbbox2 image_bounds({0, 0}, lm::dvec2(info.image_pixel_size));
        lm::dbbox2 bounds = lm::intersection(tile_bounds, image_bounds);

        if (lm::is_valid(bounds))
        {
            lm::dvec2 step = (bounds.max - bounds.min) / (double)(line_point_count - 1);

            for (int y_quad = 0; y_quad < line_point_count; y_quad++)
            {
                for (int x_quad = 0; x_quad < line_point_count; x_quad++)
                {
                    lm::dvec2 pt = step * lm::dvec2(x_quad, y_quad);
                    auto index = x_quad + y_quad * line_point_count;

                    pixel_grid_coords[index] = pt;
                    point_in_bounds[index] = true;
                    point_in_bounds_count += 1;
                }
            }
        }
    }
    else
    {
        // For some in-bounds points, no mipmap level could have been computed.
        // (For a lack of in-bounds neighbours.)
        // In this case, we propagate the mipmap levels from the closest points
        // whose levels have been determinated.
        if (points_with_mipmap_level < point_in_bounds_count)
        {
            HRZ_SCOPED_SAMPLE("propagate mipmap levels");

            for (int y_quad = 0; y_quad < line_point_count; y_quad++)
            {
                for (int x_quad = 0; x_quad < line_point_count; x_quad++)
                {
                    unsigned int point_x_index = (y_quad * line_point_count + x_quad) * 2;
                    unsigned int point_y_index = point_x_index + 1;
                    unsigned int point_mipmap_index = point_x_index / 2;

                    if (!point_in_bounds[point_mipmap_index]
                        || mipmap_levels[point_mipmap_index] != -1)
                    {
                        continue;
                    }

                    int64_t shortest_distance = std::numeric_limits<int64_t>::max();

                    // @Todo This is O(n^2), so it's not very good. Find another algorithm if it
                    // proves to be a problem.

                    for (int y_quad_2 = 0; y_quad_2 < line_point_count; y_quad_2++)
                    {
                        for (int x_quad_2 = 0; x_quad_2 < line_point_count; x_quad_2++)
                        {
                            unsigned int point_x_index_2 =
                                (y_quad_2 * line_point_count + x_quad_2) * 2;
                            unsigned int point_y_index_2 = point_x_index_2 + 1;
                            unsigned int point_mipmap_index_2 = point_x_index_2 / 2;

                            if (!point_in_bounds[point_mipmap_index_2]
                                || mipmap_levels[point_mipmap_index_2] == -1)
                            {
                                continue;
                            }

                            int64_t distance_x = point_x_index - point_x_index_2;
                            int64_t distance_y = point_y_index - point_y_index_2;

                            // We use L1 norm because we're computing distance on a grid
                            int64_t distance = distance_x + distance_y;

                            if (distance < shortest_distance)
                            {
                                mipmap_levels[point_mipmap_index] =
                                    mipmap_levels[point_mipmap_index_2];
                                shortest_distance = distance;
                            }
                        }
                    }
                }
            }
        }
    }

    // For each in-bounds point whose mipmap level has been determinated,
    // convert from mipmap level to LOD and compute the tiled image tile
    // in which the point lies.
    for (int i = 0; i < grid_point_count; i++)
    {
        if (!point_in_bounds[i] || mipmap_levels[i] == -1) continue;

        auto mipmap_level = mipmap_levels[i];
        auto lod = info.max_mipmap_level - mipmap_level;
        auto tile_pixel_size = info.provider_tile_pixel_size * ((uint64_t)1 << mipmap_level);

        lm::dvec2 pos;
        pos.x = pixel_grid_coords[i].x / (float)tile_pixel_size;
        if (info.tiling_origin == hrz_proto::TilingOrigin::TOP_ORIGIN)
        {
            pos.y = pixel_grid_coords[i].y / (double)tile_pixel_size;
        }
        else if (info.tiling_origin == hrz_proto::TilingOrigin::BOTTOM_ORIGIN)
        {
            pos.y = ((double)info.domain_pixel_size.y - pixel_grid_coords[i].y)
                / (double)tile_pixel_size;
        }
        else
        {
            assert(false && "Unhandled case");
        }

        auto tile_x = (int)std::floor(pos.x);
        auto tile_y = (int)std::floor(pos.y);

        selected_tiles.insert({tile_x, tile_y, (uint8_t)lod});

        // There may be cases where we are adding too many tiles because, for
        // instance, one tile goes all around the earth on the other side of
        // the image we want to rasterize.
        // It's probably very rare, but in case this happens, we bail out!
        // We'll just use the 0-0-0 tile below.
        //      -slerouzic, 2020-06-20
        // It can also happen when we're working with a low-level planet tile
        // and the min level of the raster is too high. (Think for example of
        // the planet tile 0-0-0 and a raster with a min level of 8.)
        //      -tpetillon, 2021-11-30
        if (selected_tiles.size() > MaxRasterTilesPerPlanetTile)
        {
            HRZ_LOG_WARNING(
                "Too many selected tiles for {}-{}-{}", params.tile_coords.lod,
                params.tile_coords.x, params.tile_coords.y);
            selected_tiles.clear();
            return;
        }
    }

    // Sometimes (for example when the tiled image is too small compared to
    // the reprojected tile), no points intersect the tiled image, or not enough
    // to determine a mipmap level.
    // In these cases, fallback to a LOD determined by the ratio of the pixels
    // in the input and output tiles. This is a pretty big approximation...
    if (selected_tiles.empty())
    {
        if (!approximate_mipmap_level.has_value())
        {
            compute_approximate_mipmap_level();
            assert(approximate_mipmap_level.has_value());
        }

        int mipmap_level = approximate_mipmap_level.value();
        int lod = info.max_mipmap_level - mipmap_level;
        int real_lod = lod + info.lod_offset;

        for (int y = 0; y < (int)info.level_zero_tile_count_y * (1 << real_lod); ++y)
        {
            for (int x = 0; x < (int)info.level_zero_tile_count_x * (1 << real_lod); ++x)
            {
                SignedTileCoords coords{x, y, (uint8_t)lod};
                auto tile_bounds_pixel = compute_tile_pixel_bounds(coords, info);

                if (lm::intersect_open(tile_bounds_pixel, info.image_pixel_bounds))
                {
                    selected_tiles.insert(coords);
                }
            }
        }
    }

    return;
}

struct TileSelectionStatus
{
    bool selected;
    bool parent_of_selected;
};

void split_tile(
    SignedTileCoords coords,
    std::vector<SignedTileCoords>& tiles,
    hrz::flat_hash_map<SignedTileCoords, TileSelectionStatus>& tiles_status)
{
    auto it = tiles_status.find(coords);
    if (it == std::end(tiles_status))
    {
        return;
    }

    it->second.selected = false;

    auto insert_tile = [&tiles_status, &tiles](SignedTileCoords coords)
    {
        auto it = tiles_status.find(coords);
        if (it != std::end(tiles_status))
        {
            if (it->second.parent_of_selected)
            {
                split_tile(it->first, tiles, tiles_status);
            }
            else
            {
                it->second.selected = true;
            }
        }
        else
        {
            tiles.push_back(coords);
            tiles_status.insert({coords, {true, false}});
        }
    };

    insert_tile({coords.x * 2 + 0, coords.y * 2 + 0, (uint8_t)(coords.lod + 1)});
    insert_tile({coords.x * 2 + 1, coords.y * 2 + 0, (uint8_t)(coords.lod + 1)});
    insert_tile({coords.x * 2 + 0, coords.y * 2 + 1, (uint8_t)(coords.lod + 1)});
    insert_tile({coords.x * 2 + 1, coords.y * 2 + 1, (uint8_t)(coords.lod + 1)});
};

void remove_overlaps(
    std::vector<SignedTileCoords>& tiles,
    hrz::flat_hash_map<SignedTileCoords, TileSelectionStatus>& status_by_tile)
{
    HRZ_SCOPED_SAMPLE("remove overlaps");

    // The selected tiles can be overlapping: two (or more) tiles can
    // cover the same area, if they are at different LODs.
    // However we don't want this. In order to handle transparency
    // correctly, we want each pixel of the reprojected tile to be
    // covered only once (at most).
    // This functions splits the biggers tiles into their children until
    // no overlaps occur.

    for (unsigned int tile_index = 0; tile_index < tiles.size(); tile_index++)
    {
        auto coords = tiles.at(tile_index);

        while (coords.lod > 0)
        {
            coords = coords.to_parent_tile();

            auto it = status_by_tile.find(coords);
            if (it != std::end(status_by_tile))
            {
                auto& status = it->second;
                status.parent_of_selected = true;
            }
            else
            {
                tiles.push_back(coords);

                auto status = TileSelectionStatus{false, true};
                status_by_tile.insert({coords, status});
            }
        }
    }

    for (unsigned int tile_index = 0; tile_index < tiles.size(); tile_index++)
    {
        auto coords = tiles.at(tile_index);
        auto& status = status_by_tile.at(coords);

        if (status.selected && status.parent_of_selected)
        {
            split_tile(coords, tiles, status_by_tile);
        }
    }
}

void project_tiled_image_tiles(
    const hrz::planet::RasterTileReprojParams& params,
    const pl_Crs* image_projection_crs,
    const ImageTilingInfo& info,
    const hrz::flat_hash_map<SignedTileCoords, TileSelectionStatus>& status_by_tile,
    hrz::planet::ReprojectedTiles& response)
{
    HRZ_SCOPED_SAMPLE("project tiled image tiles");

    // Compute reprojected planet tile coords in pixels in its zoom level.
    uint64_t tile_count_at_lod = 1 << params.tile_coords.lod;
    uint64_t pixel_count_at_lod = tile_count_at_lod * MERCATOR_TILE_SIZE;

    int64_t tile_x0_pixel =
        (int64_t)params.tile_coords.x * MERCATOR_TILE_SIZE - MERCATOR_BORDER_SIZE;
    int64_t tile_y0_pixel =
        (int64_t)params.tile_coords.y * MERCATOR_TILE_SIZE - MERCATOR_BORDER_SIZE;

    // Project all the selected tiles from the tiled image projection to
    // web Mercator, then add them to the job response.
    for (auto pair : status_by_tile)
    {
        HRZ_SCOPED_SAMPLE("project tiled image tile");

        auto& status = pair.second;

        if (!status.selected) continue;

        auto coords = pair.first;

        int mipmap_level = info.max_mipmap_level - coords.lod;
        uint64_t tile_pixel_size = info.provider_tile_pixel_size * ((uint64_t)1 << mipmap_level);
        lm::ilbbox2 tile_bounds_pixel = compute_tile_pixel_bounds(coords, info);

        if (!lm::intersect_open(tile_bounds_pixel, info.image_pixel_bounds))
        {
            // The tile does not exist, abort.
            continue;
        }

        // Some rasters only have full tiles (images of tile_pixel_size^2 pixels).
        // Others have smaller tiles on the edges, so that there is no data for outside
        // the bounds.
        if (!info.tiles_always_full_size)
        {
            tile_bounds_pixel.min.x =
                std::max(tile_bounds_pixel.min.x, info.image_pixel_bounds.min.x);
            tile_bounds_pixel.min.y =
                std::max(tile_bounds_pixel.min.y, info.image_pixel_bounds.min.y);
            tile_bounds_pixel.max.x =
                std::min(tile_bounds_pixel.max.x, info.image_pixel_bounds.max.x);
            tile_bounds_pixel.max.y =
                std::min(tile_bounds_pixel.max.y, info.image_pixel_bounds.max.y);
        }

        double tile_bounds_x_min_domain =
            (double)tile_bounds_pixel.min.x / (double)info.domain_pixel_size.x;
        double tile_bounds_y_min_domain =
            (double)tile_bounds_pixel.min.y / (double)info.domain_pixel_size.y;
        double tile_bounds_x_max_domain =
            (double)tile_bounds_pixel.max.x / (double)info.domain_pixel_size.x;
        double tile_bounds_y_max_domain =
            (double)tile_bounds_pixel.max.y / (double)info.domain_pixel_size.y;

        // Invert y min and max, because the projected coordinates go in the other direction
        // than the pixels and the tile coords.
        double tile_bounds_x_min_units =
            tile_bounds_x_min_domain * info.domain_bounds_size.x + info.domain_bounds.min.x;
        double tile_bounds_y_min_units =
            (1.0 - tile_bounds_y_max_domain) * info.domain_bounds_size.y + info.domain_bounds.min.y;
        double tile_bounds_x_max_units =
            tile_bounds_x_max_domain * info.domain_bounds_size.x + info.domain_bounds.min.x;
        double tile_bounds_y_max_units =
            (1.0 - tile_bounds_y_min_domain) * info.domain_bounds_size.y + info.domain_bounds.min.y;

        double tile_bounds_width_units = tile_bounds_x_max_units - tile_bounds_x_min_units;
        double tile_bounds_height_units = tile_bounds_y_max_units - tile_bounds_y_min_units;

        // Compute how much of the tiled image tile, if it was `image_tile_size` by
        // `image_tile_size` pixels^2, would be covered by the tiled image.
        // This is not always 1.0 because the tiles on the last row or
        // line can ben smaller, depending on the size of the whole tiled image.
        // This is computed in order to use a reasonable number of quad for the
        // projection.

        float image_tile_coverage_x = 1.0f;
        float image_tile_coverage_y = 1.0f;
        if (!info.tiles_always_full_size)
        {
            int64_t covered_pixels_x = tile_pixel_size;
            covered_pixels_x -= std::max(
                info.image_pixel_bounds.min.x - coords.x * (int64_t)tile_pixel_size, (int64_t)0);
            covered_pixels_x -=
                std::max((coords.x + 1) - info.image_pixel_bounds.max.x, (int64_t)0);
            image_tile_coverage_x = (float)covered_pixels_x / (float)tile_pixel_size;

            int64_t covered_pixels_y = tile_pixel_size;
            covered_pixels_y -= std::max(
                info.image_pixel_bounds.min.y - coords.y * (int64_t)tile_pixel_size, (int64_t)0);
            covered_pixels_y -=
                std::max((coords.y + 1) - info.image_pixel_bounds.max.y, (int64_t)0);
            image_tile_coverage_y = (float)covered_pixels_y / (float)tile_pixel_size;
        }

        // Make a grid of points on the tiled image tile.

        int quad_count_x = std::max(
            1, (int)std::ceil(image_tile_coverage_x * ATLAS_TILE_SIZE / (float)params.quad_size));
        int quad_count_y = std::max(
            1, (int)std::ceil(image_tile_coverage_y * ATLAS_TILE_SIZE / (float)params.quad_size));

        int point_count_x = quad_count_x + 1;
        int point_count_y = quad_count_y + 1;
        int total_point_count = point_count_x * point_count_y;

        assert(point_count_x >= 2);
        assert(point_count_y >= 2);

        // Compute the positions of the grid points in the tiled image projection.

        std::vector<double> grid_coords(total_point_count * 3);

        for (int y_quad = 0; y_quad < point_count_y; y_quad++)
        {
            // The y-axis is inverted when converting from UV coords (southwards) to metres
            // (northwards).
            double y_image = y_quad / (double)quad_count_y;
            double y_proj = std::max(
                tile_bounds_y_max_units - y_image * tile_bounds_height_units,
                tile_bounds_y_min_units);

            for (int x_quad = 0; x_quad < point_count_x; x_quad++)
            {
                double x_image = x_quad / (double)quad_count_x;
                double x_proj = std::min(
                    tile_bounds_x_min_units + x_image * tile_bounds_width_units,
                    tile_bounds_x_max_units);

                unsigned int x_index = (y_quad * point_count_x + x_quad) * 3;
                unsigned int y_index = x_index + 1;
                unsigned int z_index = x_index + 2;
                grid_coords[x_index] = x_proj;
                grid_coords[y_index] = y_proj;
                grid_coords[z_index] = 0;
            }
        }

        // Compute the clipping rectangle in UV coordinates. (0, 0)-(1, 1) means
        // that the whole tile is displayed. This is used to deal with tiles
        // that partially intersect the displayable bounds of the raster.
        lm::bbox2 uv_clipping;
        {
            lm::dbbox2 tile_bounds = lm::dbbox2(
                lm::dvec2(tile_bounds_x_min_units, tile_bounds_y_min_units),
                lm::dvec2(tile_bounds_x_max_units, tile_bounds_y_max_units));

            lm::dbbox2 clipped = lm::intersection(tile_bounds, info.raster_bounds);
            lm::dvec2 tile_bounds_size_units(tile_bounds_width_units, tile_bounds_height_units);

            uv_clipping.min = lm::vec2((clipped.min - tile_bounds.min) / tile_bounds_size_units);
            uv_clipping.max = lm::vec2((clipped.max - tile_bounds.min) / tile_bounds_size_units);

            // Reverse the Y axis
            std::swap(uv_clipping.min.y, uv_clipping.max.y);
            uv_clipping.min.y = 1.0f - uv_clipping.min.y;
            uv_clipping.max.y = 1.0f - uv_clipping.max.y;
        }

        // Transform the positions to web Mercator.
        {
            HRZ_SCOPED_SAMPLE("image to web mercator projection");

            // @Todo Some positions may not be expressed in web Mercator,
            // because they are out of bounds. (e.g. At the poles.)
            // In these cases `pl_transform()` returns nonsensical numerical
            // values that can look like valid values. It would be better
            // if `NaN` was returned instead.

            pl_Transform image_to_web_mercator_transform;
            pl_bake_transform(
                image_projection_crs, &hrz_proj::wmerc, &image_to_web_mercator_transform);
            pl_transform(
                &image_to_web_mercator_transform, total_point_count, grid_coords.data(),
                grid_coords.data() + 1, grid_coords.data() + 2, sizeof(double) * 3,
                grid_coords.data(), grid_coords.data() + 1, grid_coords.data() + 2,
                sizeof(double) * 3);
        }

        // Detect if the point positions must be shifted by a whole planet revolution.
        // Sometimes a tile gets projected to one side of the web Mercator space,
        // but the planet tile we are dealing with is on the other side. But some pixels
        // must still be rasterised, because of the roundness of the planet. If nothing
        // is done, the tile is out of bounds and does not get rasterised at all.
        // To counter this, the positions are shifted by a whole planet revolution.
        // After that, the tile is projected to the correct side of the web Mercator
        // space.
        // This is safe because it corresponds to the same positions on the planet.
        //
        // Sometimes the same tiled image tile can appear at multiple places
        // on a single planet tile (again, because of the roundess of the planet).
        // To handle this case correctly, the tiled image tile must be rasterised
        // more than once.

        double planet_tile_center_meter =
            (MERCATOR_RANGE / (double)tile_count_at_lod) * (params.tile_coords.x + 0.5)
            - HALF_MERCATOR_RANGE;
        double image_tile_center_meter =
            grid_coords[((point_count_y / 2) * point_count_x + point_count_x / 2) * 3 + 0];

        // Indicates which shifts must be made, and in which direction.
        // (-1: To the West, 1: to the East)
        std::array<int, 3> revolutions = {0, 0, 0};

        if (params.tile_coords.lod == 0)
        {
            // Planet tile 0-0-0: Draw everything three times.
            // It's a lot, but in some cases it's the only way to be sure everything
            // is properly rasterised. This tile is only rarely composed anyway.
            revolutions[1] = -1;
            revolutions[2] = 1;
        }
        else if (params.tile_coords.lod == 1)
        {
            // Planet tile 1-x-y: Draw the image tile twice if its centre is far from
            // the centre of the planet tile.
            // If the image tile is large, it may appear on both sides of the planet tile.
            if (image_tile_center_meter - planet_tile_center_meter > HALF_MERCATOR_RANGE * 0.9)
            {
                revolutions[1] = -1;
            }
            else if (
                image_tile_center_meter - planet_tile_center_meter < -HALF_MERCATOR_RANGE * 0.9)
            {
                revolutions[1] = 1;
            }
        }
        else
        {
            // Planet tiles with LOD greater than 1: Shift the image tile if necessary.
            // Test if the distance between the tiled image tile and planet tile centres
            // (in web Mercator) suggests that a shift is needed. If so, apply it.
            if (image_tile_center_meter - planet_tile_center_meter > HALF_MERCATOR_RANGE)
            {
                revolutions[0] = -1;
            }
            else if (image_tile_center_meter - planet_tile_center_meter < -HALF_MERCATOR_RANGE)
            {
                revolutions[0] = 1;
            }
        }

        for (unsigned int r = 0; r < revolutions.size(); r++)
        {
            int revolution = revolutions[r];

            if (r > 0 && revolution == 0) break;

            response.tiles.emplace_back();
            auto& tile = response.tiles.back();
            tile.coords.x = coords.x;
            tile.coords.y = coords.y;
            tile.coords.lod = (uint8_t)(coords.lod + info.lod_offset);
            tile.grid_size.x = point_count_x;
            tile.grid_size.y = point_count_y;
            tile.grid_coords.reserve(total_point_count);
            tile.uv_clip = uv_clipping;

            // Convert the grid point coordinates from web Mercator to the UV-space
            // of the planet tile, taking the optional shift into account, and add
            // the values to the response.
            for (unsigned int i = 0; i < (unsigned int)total_point_count; i++)
            {
                double x_meter = grid_coords[i * 3 + 0];
                double y_meter = grid_coords[i * 3 + 1];

                // Add the planet revolution shift.
                x_meter += MERCATOR_RANGE * revolution;

                // The y-axis is inverted when converting from metres (northwards) to pixels
                // (southwards).
                double x_pixel =
                    (((x_meter / HALF_MERCATOR_RANGE) + 1) / 2) * (double)pixel_count_at_lod;
                double y_pixel =
                    (1 - (((y_meter / HALF_MERCATOR_RANGE) + 1) / 2)) * (double)pixel_count_at_lod;

                double x = (x_pixel - (double)tile_x0_pixel)
                    / (MERCATOR_TILE_SIZE + 2 * MERCATOR_BORDER_SIZE);
                double y = (y_pixel - (double)tile_y0_pixel)
                    / (MERCATOR_TILE_SIZE + 2 * MERCATOR_BORDER_SIZE);

                tile.grid_coords.push_back((float)x);
                tile.grid_coords.push_back((float)y);
            }
        }
    }
}

void compute_tiled_image_reprojection(
    const hrz::planet::RasterTileReprojParams& params,
    const pl_Crs* image_projection_crs,
    hrz::planet::ReprojectedTiles& response)
{
    HRZ_SCOPED_SAMPLE("compute tiled image reprojection");

    // * Project the planet tile from web Mercator to the tiled image projection.
    // * Use this to determined on which tiled image tiles it falls.
    // * Select these tiles.
    // * Split the tiles where necessary so that no overlaps occur.
    // * Project the tiles to web Mercator.

    const auto& raster_geometry = params.raster_geometry;

    if (raster_geometry.tiling_scheme.type() == hrz_proto::GLOBAL)
    {
        assert(raster_geometry.tiling_scheme.has_global_tiling());
        assert(raster_geometry.tiling_scheme.global_tiling().tile_size() > 0);
        assert(raster_geometry.tiling_scheme.global_tiling().level_zero_tile_count_x() > 0);
        assert(raster_geometry.tiling_scheme.global_tiling().level_zero_tile_count_y() > 0);
    }
    else if (raster_geometry.tiling_scheme.type() == hrz_proto::LOCAL)
    {
        assert(raster_geometry.tiling_scheme.has_local_tiling());
        assert(raster_geometry.tiling_scheme.local_tiling().full_image_width() > 0);
        assert(raster_geometry.tiling_scheme.local_tiling().full_image_height() > 0);
        assert(raster_geometry.tiling_scheme.local_tiling().tile_size() > 0);
    }
    else
    {
        assert(false && "Unhandled case");
    }

    ImageTilingInfo image_tiling_info =
        compute_image_tiling_info(raster_geometry, image_projection_crs);

    hrz::flat_hash_set<SignedTileCoords> selected_tiles;
    select_tiled_image_tiles(params, image_projection_crs, image_tiling_info, selected_tiles);

    std::vector<SignedTileCoords> tile_list;
    hrz::flat_hash_map<SignedTileCoords, TileSelectionStatus> status_by_tile;

    for (auto coords : selected_tiles)
    {
        tile_list.push_back(coords);
        status_by_tile.insert({coords, {true, false}});
    }

    remove_overlaps(tile_list, status_by_tile);

    project_tiled_image_tiles(
        params, image_projection_crs, image_tiling_info, status_by_tile, response);
}

hrz::JobResult run(
    const hrz::planet::RasterTileReprojParams& params,
    hrz::planet::ReprojectedTiles& response,
    const JobContext&)
{
    HRZ_SCOPED_SAMPLE("reproject tile job");

    assert(params.quad_size >= 1);

    const auto& raster_geometry = params.raster_geometry;

    pl_Crs web_mercator_crs = hrz_proj::wmerc;

    pl_Crs param_crs;
    bool convert_success = hrz::convert_crs(raster_geometry.projection, &param_crs);
    if (!convert_success) return hrz::JobResult::FAILURE;

    const auto tiling_scheme_type = raster_geometry.tiling_scheme.type();
    if (tiling_scheme_type == hrz_proto::TilingSchemeType::GLOBAL
        && pl_are_crs_equal(&param_crs, &web_mercator_crs)
        && raster_geometry.tiling_scheme.global_tiling().tile_size() == MERCATOR_TILE_SIZE
        && raster_geometry.tiling_scheme.global_tiling().level_zero_tile_count_x() == 1
        && raster_geometry.tiling_scheme.global_tiling().level_zero_tile_count_y() == 1
        && raster_geometry.tiling_scheme.global_tiling().border_tile_aspect()
            == hrz_proto::BorderTileAspect::FULL_SIZED)
    {
        // This is Horizon's native projection and tiling scheme.
        // Go through the fast-path reprojection, that avoids doing any actual
        // projection computations, as tiles from the raster layer are trivially
        // mapped to planet tiles.

        compute_tiled_mercator_reprojection(params, response);
    }
    else
    {
        // The more versatile, but also more computation-heavy path, for other cases.

        compute_tiled_image_reprojection(params, &param_crs, response);
    }

    return hrz::JobResult::SUCCESS;
}

} // namespace hrz_jobs::reproject_raster_tile
