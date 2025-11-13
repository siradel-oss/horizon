#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/crs_database.h"
#include "hrz/common/image_processing.h"
#include "hrz/common/image_view.h"
#include "hrz/common/planet.h"
#include "hrz/common/proj.h"
#include "hrz/common/proto_maths.h"
#include "hrz/common/raster_sampling.h"
#include "hrz/common/reprojection.h"
#include "hrz/common/tile_coords.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/fnd/array_view.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/log.h"

#include <optional>

using namespace hrz;

namespace
{

// The higher this number is, the finer the point query is, but the higher the
// number of requested tiles also becomes. We may need to make this
// configurable.
constexpr size_t kMaxRasterTilePerBatch = 4;

void get_tile(
    const hrz::ImageTilingInfo& info,
    const lm::dvec2& domain_pos,
    TileCoords& out_tile,
    lm::dvec2& out_domain_coords_pixel)
{
    lm::dvec2 domain_coords = {
        (domain_pos.x - info.domain_bounds.min.x) / info.domain_bounds_size.x,
        (info.domain_bounds.max.y - domain_pos.y) / info.domain_bounds_size.y,
    };

    lm::dvec2 domain_coords_pixel(domain_coords * info.domain_pixel_size);
    hrz::TileCoords tile = hrz::TileCoords{
        (uint32_t)std::floor(domain_coords_pixel.x / info.provider_tile_pixel_size),
        (uint32_t)std::floor(domain_coords_pixel.y / info.provider_tile_pixel_size),
        info.max_lod,
    };

    out_tile = tile;
    out_domain_coords_pixel = domain_coords_pixel;
}

hrz::JobResult cull(
    uint32_t raster_index,
    const hrz::planet::TiledRasterGeometry& raster_geometry,
    const lm::dbbox2& raster_display_bounds,
    hrz::ArrayView<const lm::dvec2> points_2d,
    hrz::planet::CulledPointsQuery& culled,
    BlobAllocator* blob_allocator)
{
    pl_Crs from = hrz_proj::wmerc;
    pl_Crs to;
    bool ok = hrz::convert_crs(raster_geometry.projection, &to);
    (void)ok; // Prevent "unused variable" warning when assertions are disabled.
    assert(ok);

    hrz::ImageTilingInfo tiling_info = compute_image_tiling_info(raster_geometry, &to);

    pl_Transform xform;
    ok = pl_bake_transform(&from, &to, &xform);
    assert(ok == pl_Result_Ok);

    size_t point_count = points_2d.size();
    if (point_count == 0) return hrz::JobResult::SUCCESS;

    hrz::BlobVector<lm::dvec3> points(blob_allocator, point_count);
    points.reserve(point_count);
    for (unsigned int i = 0; i < points_2d.size(); ++i)
    {
        points.push_back(lm::dvec3{points_2d[i].x, points_2d[i].y, 0});
    }
    auto points_data_opt = points.data();
    if (!points_data_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate data.");
        return hrz::JobResult::FAILURE;
    }
    auto points_data = points_data_opt.value();

    double* data = (double*)points_data.data();
    ok = pl_transform_in_place_canonical(&xform, points_data.size(), data);
    assert(ok == pl_Result_Ok);

    hrz::flat_hash_set<TileCoords> tiles;

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto& pt = points_data[i];

        if (!lm::contains(tiling_info.raster_bounds, pt.xy)) continue;
        if (!lm::contains(raster_display_bounds, pt.xy)) continue;

        TileCoords tile;
        lm::dvec2 out_pos;
        get_tile(tiling_info, pt.xy, tile, out_pos);

        tiles.insert(tile);
    }

    hrz::flat_hash_set<TileCoords> tiles_swap;
    while (tiles.size() > kMaxRasterTilePerBatch && tiles.begin()->lod > tiling_info.min_lod)
    {
        for (const TileCoords& tile : tiles)
        {
            tiles_swap.insert(tile.parent());
        }

        tiles_swap.swap(tiles);
        tiles_swap.clear();
    }

    for (const TileCoords& coords : tiles)
    {
        culled.tiles.push_back({raster_index, coords});
    }

    return hrz::JobResult::SUCCESS;
}

hrz::JobResult cull(
    const hrz::planet::CullPointsQueryParams& params,
    hrz::ArrayView<const lm::dvec2> points,
    hrz::planet::CulledPointsQuery& resp,
    BlobAllocator* blob_allocator)
{
    if (points.size() == 0)
    {
        HRZ_LOG_WARNING("Cull job with an empty list of points.");
        return hrz::JobResult::SUCCESS;
    }

    uint32_t index = 0;
    for (const auto& raster : params.rasters)
    {
        if (cull(index++, raster.geometry, raster.display_bounds, points, resp, blob_allocator)
            != hrz::JobResult::SUCCESS)
        {
            return hrz::JobResult::FAILURE;
        }
    }

    return hrz::JobResult::SUCCESS;
}
} // anonymous namespace

namespace hrz_jobs
{
namespace cull_points_query
{
hrz::JobResult run(
    const hrz::planet::CullPointsQueryParams& params,
    hrz::planet::CulledPointsQuery& resp,
    const JobContext& context)
{
    if (std::holds_alternative<hrz::BlobArrayView<lm::dvec2>>(params.points))
    {
        auto point_data = std::get<hrz::BlobArrayView<lm::dvec2>>(params.points).get_data_view();
        return cull(params, point_data.as_array_view(), resp, context.get_blob_allocator());
    }
    else
    {
        assert(std::holds_alternative<lm::dvec2>(params.points));
        auto point_view = hrz::ArrayView<const lm::dvec2>(&std::get<lm::dvec2>(params.points), 1);
        return cull(params, point_view, resp, context.get_blob_allocator());
    }
}
} // namespace cull_points_query

namespace
{
hrz::JobResult sample(
    const hrz::planet::SamplePointsQueryParams& params,
    hrz::ArrayView<const lm::dvec2> points,
    hrz::planet::SampledPointsQuery& resp,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    if (points.size() == 0)
    {
        HRZ_LOG_WARNING("Sample job with an empty list of points.");
        return hrz::JobResult::SUCCESS;
    }

    auto check_image_format = [](hrz_proto::ImageFormat format)
    {
        if (!hrz::is_scalar_image_format(format))
        {
            HRZ_LOG_ERROR(
                "Invalid image format for elevation query: {} is not a scalar format.",
                hrz_proto::ImageFormat_Name(format));
            return false;
        }
        return true;
    };

    for (const auto& raster : params.rasters)
    {
        if (!check_image_format(raster.image_format))
        {
            return hrz::JobResult::FAILURE;
        }
    }

    for (const auto& tile : params.tiles)
    {
        auto format = tile.image.proto_format();
        if (!format.has_value() || !check_image_format(format.value()))
        {
            return hrz::JobResult::FAILURE;
        }
    }

    const size_t point_count = points.size();
    if (point_count == 0) return hrz::JobResult::FAILURE;

    // This marks points that we need to continue sampling and the ones we don't.
    std::vector<bool> to_sample(point_count, true);

    hrz::BlobVector<lm::dvec3> point_coords(blob_allocator, point_count);
    for (size_t i = 0; i < point_count; ++i)
    {
        point_coords.push_back({points[i].x, points[i].y, 0.0});
    }
    auto point_coords_data_opt = point_coords.data();
    if (!point_coords_data_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate data.");
        return hrz::JobResult::FAILURE;
    }
    auto point_coords_data = point_coords_data_opt.value();

    hrz::flat_hash_map<TileCoords, uint32_t> tile_indices;
    hrz::flat_hash_map<TileCoords, std::optional<TileCoords>> cached_available_tiles;

    hrz::BlobVector<float> elevations(blob_allocator, point_count);
    elevations.register_blob_metadata("contents"_ss, "sampled points elevations"_ss);
    elevations.register_blob_owner(resource_owner);
    elevations.resize(point_count);
    auto elevations_data_opt = elevations.data();
    if (!elevations_data_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate data.");
        return hrz::JobResult::FAILURE;
    }
    auto elevations_data = elevations_data_opt.value();

    // Sample rasters from top to bottom.
    for (int32_t raster_index = (int32_t)params.rasters.size() - 1; raster_index >= 0;
         --raster_index)
    {
        const auto& raster = params.rasters.at(raster_index);

        // Gather all tiles for this raster.
        tile_indices.clear();
        for (size_t tile_index = 0; tile_index < (size_t)params.tiles.size(); ++tile_index)
        {
            const auto& tile = params.tiles.at(tile_index);
            if (tile.raster_index == (uint32_t)raster_index)
            {
                tile_indices.insert({tile.tile_coords, (uint32_t)tile_index});
            }
        }

        // Transform all points to local coordinate system
        pl_Crs from = hrz_proj::wmerc;
        pl_Crs to;
        bool ok = hrz::convert_crs(raster.geometry.projection, &to);
        assert(ok);
        (void)ok; // Prevent "unused variable" warning when assertions are disabled.

        hrz::ImageTilingInfo tiling_info = compute_image_tiling_info(raster.geometry, &to);

        pl_Transform xform;
        ok = pl_bake_transform(&from, &to, &xform);
        assert(ok == pl_Result_Ok);

        const double* src_ptr = (const double*)point_coords_data.data();
        double* dst_ptr = (double*)point_coords_data.data();
        pl_transform_canonical(&xform, point_count, src_ptr, dst_ptr);

        const auto& nodata_params = raster.nodata;
        const auto& sampling_params = raster.sampling;
        const auto& blending_params = raster.blending;

        hrz::sampling::NodataFunction nodata_function(
            nodata_params.value(), sampling_params.nodata_handling(), raster.image_format);

        decltype(&hrz::sampling::fetch_r_f32_pixel) pixel_fetch_function = nullptr;
        switch (raster.image_format)
        {
            case hrz_proto::ImageFormat::R_F32:
                pixel_fetch_function = hrz::sampling::fetch_r_f32_pixel;
                break;
            case hrz_proto::ImageFormat::R_F32_SILICIUM:
                pixel_fetch_function = hrz::sampling::fetch_r_f32_silicium_pixel;
                break;
            case hrz_proto::ImageFormat::SIGNED_FIXED_24_8:
                pixel_fetch_function = hrz::sampling::fetch_signed_fixed_24_8_pixel;
                break;
            case hrz_proto::ImageFormat::TERRARIUM:
                pixel_fetch_function = hrz::sampling::fetch_terrarium_pixel;
                break;
            case hrz_proto::ImageFormat::TERRAIN_RGB:
                pixel_fetch_function = hrz::sampling::fetch_terrain_rgb_pixel;
                break;
            default: assert(false && "Unhandled case");
        }

        hrz::sampling::DtmSamplingFunction sampling_function(
            pixel_fetch_function, sampling_params.alpha_channel_usage(), nodata_function,
            sampling_params.filtering());

        hrz::sampling::DtmBlendingFunction blending_function(blending_params.opacity());

        cached_available_tiles.clear();

        // Iterate on all points to sample them.
        for (size_t i = 0; i < point_count; ++i)
        {
            if (!to_sample[i]) continue;

            lm::dvec3 pt = point_coords_data[i];

            if (!lm::contains(tiling_info.raster_bounds, pt.xy)) continue;
            if (!lm::contains(raster.display_bounds, pt.xy)) continue;

            lm::dvec2 domain_pixel;
            TileCoords tile_coords;
            get_tile(tiling_info, pt.xy, tile_coords, domain_pixel);

            unsigned int tile_image_index = 0;
            TileCoords available_tile_coords;
            bool found_tile_image = false;
            TileCoords starting_tile_coords = tile_coords;

            // Here we look up the tile pyramid to find a tile that is available
            // for clamping, and we cached the result for other point queries of
            // the same raster.

            auto it = cached_available_tiles.find(tile_coords);
            if (it != cached_available_tiles.end())
            {
                if (it->second.has_value())
                {
                    found_tile_image = true;
                    available_tile_coords = it->second.value();
                    tile_image_index = tile_indices.find(available_tile_coords)->second;
                }
                else
                {
                    found_tile_image = false;
                }
            }
            else
            {
                while (tile_coords.lod > tiling_info.min_lod)
                {
                    auto it = tile_indices.find(tile_coords);
                    if (it != tile_indices.end())
                    {
                        available_tile_coords = tile_coords;
                        found_tile_image = true;
                        tile_image_index = it->second;
                        cached_available_tiles.insert(
                            std::make_pair(starting_tile_coords, tile_coords));
                        break;
                    }
                    else
                    {
                        tile_coords = tile_coords.parent();
                    }
                }

                if (!found_tile_image)
                {
                    cached_available_tiles.insert(
                        std::make_pair(starting_tile_coords, std::nullopt));
                }
            }

            if (found_tile_image)
            {
                int lod_diff = starting_tile_coords.lod - available_tile_coords.lod;
                assert(lod_diff >= 0);

                domain_pixel *= std::pow(0.5, lod_diff);

                lm::vec2 image_pixel =
                    lm::vec2(
                        std::fmod(
                            (double)domain_pixel.x, (double)tiling_info.provider_tile_pixel_size),
                        std::fmod(
                            (double)domain_pixel.y, (double)tiling_info.provider_tile_pixel_size))
                    / (float)tiling_info.provider_tile_pixel_size;

                const auto& tile_image = params.tiles.at(tile_image_index);
                auto image_data = tile_image.image.data();

                ImageView image(
                    image_data, tile_image.image.proto_format().value(), tile_image.image.width(),
                    tile_image.image.height());

                float* elevation = &elevations_data[i];

                // For now we only sample DTMs as opaque so this is OK.
                // In the future we may need to change this, for instance we may
                // have to sample from bottom to top raster.
                bool sampled = hrz::sampling::sample_and_compose_raster<float, 1>(
                    image, image_pixel, &sampling_function, &blending_function, elevation);

                if (sampled) to_sample[i] = false;
            }
        }
    }

    auto elevations_array_opt = elevations.to_blob_array();
    if (!elevations_array_opt.has_value())
    {
        return hrz::JobResult::FAILURE;
    }

    resp.values = std::move(elevations_array_opt.value());

    return hrz::JobResult::SUCCESS;
}
} // namespace

namespace sample_points_query
{
hrz::JobResult run(
    const hrz::planet::SamplePointsQueryParams& params,
    hrz::planet::SampledPointsQuery& resp,
    const JobContext& context)
{
    if (std::holds_alternative<hrz::BlobArrayView<lm::dvec2>>(params.points))
    {
        auto point_data = std::get<hrz::BlobArrayView<lm::dvec2>>(params.points).get_data_view();
        return sample(
            params, point_data.as_array_view(), resp, context.get_blob_allocator(),
            context.get_resource_owner());
    }
    else
    {
        assert(std::holds_alternative<lm::dvec2>(params.points));
        auto point_view = hrz::ArrayView<const lm::dvec2>(&std::get<lm::dvec2>(params.points), 1);
        return sample(
            params, point_view, resp, context.get_blob_allocator(), context.get_resource_owner());
    }
}

} // namespace sample_points_query
} // namespace hrz_jobs
