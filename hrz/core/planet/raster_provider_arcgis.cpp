#include "hrz/common/crs_database.h"
#include "hrz/common/geo.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/core/base_url.h"
#include "hrz/core/planet/raster_provider.h"
#include "hrz/core/planet/tile_fetcher.h"
#include "hrz/fnd/json_utils.h"
#include "hrz/fnd/mem.h"
#include "hrz/fnd/string_utils.h"
#include "hrz/fnd/url_utils.h"

#include <fmt/ranges.h>
#include <proj_lite.h>
#include <rapidjson/document.h>

#include <cassert>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>

namespace
{
constexpr float inch_to_cm = 2.54;
constexpr float default_dpi = 96;
constexpr float pixel_size = (inch_to_cm / default_dpi) * 0.01; // meter per pixel

struct TileUrlGenerator : public hrz::TileUrlGenerator
{
    hrz::BaseUrl base_url;
    std::vector<std::string> lod_names;
    uint32_t min_lod;

    TileUrlGenerator(hrz::BaseUrl base_url, std::vector<std::string> lod_names, uint32_t min_lod) :
        base_url(std::move(base_url)), lod_names(std::move(lod_names)), min_lod(min_lod)
    {
        // We need to preserve the "MapServer" bit.
        this->base_url.add_slash();
    }

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        assert(z >= min_lod && z <= min_lod + lod_names.size());

        return base_url.derive(
            fmt::format("tile/{}/{}/{}?blankTile=false", lod_names.at(z - min_lod), y, x));
    }
};

// https://developers.arcgis.com/rest/services-reference/enterprise/export-map.htm
//
// The export map operation returns images containing colours. It can return
// images that are compositions of multiple source layers.
struct ExportMapUrlGenerator : public hrz::TileUrlGenerator
{
    hrz::BaseUrl base_url;
    int srid;
    uint32_t tile_size;
    std::string format;
    bool use_transparency;
    std::vector<int> layers;

    ExportMapUrlGenerator(
        hrz::BaseUrl base_url,
        int srid,
        uint32_t tile_size,
        const std::string& format,
        bool use_transparency,
        std::vector<int> layers) :
        base_url(std::move(base_url)),
        srid(srid),
        tile_size(tile_size),
        format(format),
        use_transparency(use_transparency),
        layers(std::move(layers))
    {
        // We need to preserve the "MapServer" bit.
        this->base_url.add_slash();
    }

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        lm::dbbox2 tile_bbox;
        if (srid == 3857)
        {
            tile_bbox = hrz::mercator_tile_bbox_meters({x, y, (uint8_t)z});
        }
        else
        {
            auto bounds = hrz::geodetic_tile_bounds({x, y, (uint8_t)z});
            tile_bbox = lm::dbbox2{
                {lm::degrees(bounds.west), lm::degrees(bounds.south)},
                {lm::degrees(bounds.east), lm::degrees(bounds.north)}};
        }

        std::string layer_query = "";
        if (!layers.empty())
        {
            layer_query = "&layers=show:" + fmt::to_string(fmt::join(layers, ","));
        }

        return base_url.derive(fmt::format(
            "export?F=image&FORMAT={}{}&SIZE={},{}&BBOX={},{},{},{}&BBOXSR={}&IMAGESR={}{}", format,
            layer_query, tile_size, tile_size, tile_bbox.min.x, tile_bbox.min.y, tile_bbox.max.x,
            tile_bbox.max.y, srid, srid, use_transparency ? "&transparent=true" : ""));
    }
};

// https://developers.arcgis.com/rest/services-reference/enterprise/export-image.htm
//
// The export image operation supports returning scalar images (which can have
// nodata values). And even multiband scalar images.
// It doesn't support composing multiple layers.
// It can request that a rendering rule be applied to the data before being returned
// by the server. (A function that converts scalar data to colours.)
// Scalar data can only be returned in TIFF images.
//
// See here for an example:
// https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer
//
// @Todo(HRZ-190) Request TIFF images and set the pixelType parameters once
//                TIFF files can be decoded.
//                Allow setting the image format in the model to float.
struct ExportImageUrlGenerator : public hrz::TileUrlGenerator
{
    std::string base_url;
    int srid;
    uint32_t tile_size;
    std::string format;

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        lm::dbbox2 tile_bbox;
        if (srid == 3857)
        {
            tile_bbox = hrz::mercator_tile_bbox_meters({x, y, (uint8_t)z});
        }
        else
        {
            auto bounds = hrz::geodetic_tile_bounds({x, y, (uint8_t)z});
            tile_bbox = lm::dbbox2{
                {lm::degrees(bounds.west), lm::degrees(bounds.south)},
                {lm::degrees(bounds.east), lm::degrees(bounds.north)}};
        }

        return fmt::format(
            "{}/exportImage?f=image&format={}&size={},{}&bbox={},{},{},{}&bboxSR={}&imageSR={}",
            base_url, format, tile_size, tile_size, tile_bbox.min.x, tile_bbox.min.y,
            tile_bbox.max.x, tile_bbox.max.y, srid, srid);
    }
};

uint32_t canonicalize_srid(uint32_t srid)
{
    // why use one code when you could use 3 !
    // https://support.esri.com/en/technical-article/000013950
    // https://epsg.io/102113
    // https://epsg.io/102100
    if (srid == 102100 || srid == 102113) return 3857;

    return srid;
}

struct Extent
{
    uint32_t srid;
    lm::dbbox2 bounds;
};

std::optional<Extent> get_extent(const rapidjson::Value& extent_node)
{
    if (!extent_node.HasMember("spatialReference"))
    {
        return std::nullopt;
    }

    const auto& spatial_reference = extent_node["spatialReference"];
    int srid = canonicalize_srid(hrz::json::get_int_or(
        spatial_reference, spatial_reference.HasMember("latestWkid") ? "latestWkid" : "wkid", -1));

    if (srid < 0)
    {
        return std::nullopt;
    }

    Extent extent;
    extent.srid = (uint32_t)srid;
    extent.bounds.min.x = hrz::json::get_double_or(extent_node, "xmin", 0);
    extent.bounds.min.y = hrz::json::get_double_or(extent_node, "ymin", 0);
    extent.bounds.max.x = hrz::json::get_double_or(extent_node, "xmax", 0);
    extent.bounds.max.y = hrz::json::get_double_or(extent_node, "ymax", 0);

    return {extent};
}

Extent reproject_extent(const Extent& extent, uint32_t srs)
{
    if (extent.srid == srs) return extent;

    // The corners of a bounding box are not necessarily the
    // corners of its reprojection. To counter this, multiple
    // positions are reprojected along each side of the bbox.
    //
    // Indices:
    //
    //  9  10  11  12  13  14  15  16  17
    //
    // 24                              31
    //
    // 23                              30
    //
    // 22                              29
    //
    // 21                              28
    //
    // 20                              27
    //
    // 19                              26
    //
    // 18                              25
    //
    //  0   1   2   3   4   5   6   7   8

    constexpr size_t point_count = 32;
    lm::dvec3 bounds[point_count];
    {
        lm::dvec3 sw = {lm::corner(extent.bounds, 0), 0};
        lm::dvec3 se = {lm::corner(extent.bounds, 1), 0};
        lm::dvec3 nw = {lm::corner(extent.bounds, 2), 0};
        lm::dvec3 ne = {lm::corner(extent.bounds, 3), 0};

        for (size_t i = 0; i < 9; ++i)
        {
            double t = (double)i / 8;
            bounds[i + 0] = lm::mix(sw, se, t);
            bounds[i + 9] = lm::mix(nw, ne, t);
        }
        for (size_t i = 1; i < 8; ++i)
        {
            double t = (double)i / 8;
            bounds[i + 17] = lm::mix(sw, nw, t);
            bounds[i + 24] = lm::mix(se, ne, t);
        }
    }

    pl_Crs from_srs;
    hrz::convert_crs(fmt::format("EPSG:{}", extent.srid).c_str(), &from_srs);

    pl_Crs to_srs;
    hrz::convert_crs(fmt::format("EPSG:{}", srs).c_str(), &to_srs);

    pl_Transform transform;
    pl_bake_transform(&from_srs, &to_srs, &transform);
    pl_transform_in_place_canonical(&transform, point_count, (double*)bounds);

    Extent reprojected_extent;
    reprojected_extent.srid = srs;
    reprojected_extent.bounds = lm::dbbox2::invalid();

    for (size_t i = 0; i < point_count; ++i)
    {
        reprojected_extent.bounds = lm::expand(reprojected_extent.bounds, bounds[i].xy);
    }

    return reprojected_extent;
}

// "Mixed" image format uses JPEG for non-transparent tiles, and PNG for
// transparent tiles (usually at the border of the area with data).
//
// https://desktop.arcgis.com/en/arcmap/10.3/map/publish-map-services/24004-cache-image-format-should-be-jpeg-or-mixed-as-map-contains-only-raster-layers.htm
const std::string IMAGE_FORMATS[] = {"mixed", "jpgpng", "jpg",  "png32",
                                     "png",   "png24",  "png8", "gif"};

bool format_supports_transparency(std::string_view format)
{
    return format.starts_with("png") || format.ends_with("png");
}
} // namespace

namespace hrz::planet
{
bool is_provider_model_complete(const hrz_proto::ArcGisRasterProviderParams& params)
{
    return !params.url().empty();
}

hrz_proto::ImageFormat get_image_format(const hrz_proto::ArcGisRasterProviderParams&)
{
    return hrz_proto::ImageFormat::SRGBA_8;
}

class ArcGisProvider : public RasterProvider
{
public:
    ArcGisProvider(
        const hrz_proto::ArcGisRasterProviderParams& params,
        assets_loader::Queue queue,
        uint32_t default_tile_cache_size,
        uint64_t raster_id) :
        status(InternalStatus::LoadingAndParsingDescriptor),
        url(params.url()),
        headers(assets_loader::from_proto(params.http_headers())),
        additional_attribution(params.attribution()),
        preserve_query_parameters(params.preserve_query_parameters()),
        al_queue(queue),
        missing_tile_policy(params.missing_tile_policy()),
        desired_image_format(params.source_image_format()),
        layer_ids(params.layer_ids().begin(), params.layer_ids().end()),
        ignore_extent(params.ignore_bounds()),
        tile_cache_capacity(
            params.override_tile_cache_size() ? params.tile_cache_size() : default_tile_cache_size),
        download_ticket(0),
        raster_id(raster_id)
    {
    }

    hrz_proto::RasterProviderType get_raster_provider_type() const override
    {
        return hrz_proto::RasterProviderType::ARCGIS_RASTER_PROVIDER;
    }

    hrz_proto::ImageFormat get_image_format() const override { return hrz_proto::SRGBA_8; }

    void set_load_queue(assets_loader::Queue queue) override { al_queue = queue; }

    Status get_status() const override
    {
        switch (status)
        {
            case InternalStatus::LoadingAndParsingDescriptor: return Status::Loading;
            case InternalStatus::Ready: return Status::Ready;
            case InternalStatus::Error: return Status::Error;
            default: assert(false && "Unhandled case"); return Status::Error;
        }
    }

    TileStatus get_tile_status(LockTicket lock_ticket) override
    {
        if (fetcher.has_value())
        {
            return fetcher->get_tile_status((TileFetcher::LockTicket)lock_ticket);
        }
        else
        {
            return TileStatus::Loading;
        }
    }

    TileImage get_tile_image(LockTicket lock_ticket) override
    {
        assert(status == InternalStatus::Ready);
        return fetcher->get_tile_image((TileFetcher::LockTicket)lock_ticket);
    }

    const hrz::planet::TiledRasterGeometry& get_geometry() const override
    {
        assert(status == InternalStatus::Ready);

        return geometry;
    }

    const hrz_proto::RasterNodata& get_nodata() const override
    {
        // @Todo(HRZ-190) Return nodata value for scalar images.
        return hrz_proto::RasterNodata::default_instance();
    }

    void cancel_jobs_and_release_tiles(AssetsLoader* al, BlobAllocator* ba, JobScheduler* js)
        override
    {
        assert(al && js);

        if (status == InternalStatus::LoadingAndParsingDescriptor)
        {
            assets_loader::end(al, download_ticket);
        }
        else if (status == InternalStatus::Ready)
        {
            fetcher->cancel_jobs_and_release_tiles(al, ba, js);
        }
    }

    void release_tile(LockTicket lock_ticket, bool nominate_for_eviction = false) override
    {
        if (fetcher.has_value())
        {
            fetcher->release_tile((TileFetcher::LockTicket)lock_ticket, nominate_for_eviction);
        }
    }

    LockTicket request_and_lock_tile(TileCoords tile_coords, AssetsLoader* al, uint32_t priority)
        override
    {
        if (status != InternalStatus::Ready) return LockTicket::Invalid;

        return (LockTicket)fetcher->request_and_lock_tile(tile_coords, al, al_queue, priority);
    }

    LockTicket lock_tile_if_ready(TileCoords tile_coords) override
    {
        if (status != InternalStatus::Ready) return LockTicket::Invalid;

        return (LockTicket)fetcher->lock_tile_if_ready(tile_coords);
    }

private:
    // https://developers.arcgis.com/javascript/latest/api-reference/esri-layers-support-TileInfo.html
    bool parse_tile_info(
        const rapidjson::Value& tile_info_json,
        const rapidjson::Value& full_extent_json,
        const uint32_t declared_min_lod,
        const uint32_t declared_max_lod,
        AttributionHandle attribution)
    {
        int tile_size = 0;
        {
            int tile_width = hrz::json::get_int_or(tile_info_json, "rows", 0);
            int tile_height = hrz::json::get_int_or(tile_info_json, "cols", 0);

            if (tile_width != tile_height || tile_width <= 0)
            {
                return false;
            }

            tile_size = tile_width;
        }

        auto format = hrz::json::get_str_or(tile_info_json, "format", "");
        {
            if (!desired_image_format.empty() && desired_image_format != format)
            {
                return false;
            }

            bool found = false;
            for (size_t i = 0; i < HRZ_ARRAY_COUNT(IMAGE_FORMATS) && !found; ++i)
            {
                if (hrz::str::iequals(format, IMAGE_FORMATS[i]))
                {
                    found = true;
                }
            }

            if (!found)
            {
                return false;
            }
        }

        auto& spatial_reference_json =
            hrz::json::get_member_or_null(tile_info_json, "spatialReference");
        if (spatial_reference_json.IsNull())
        {
            return false;
        }

        int srid = canonicalize_srid(hrz::json::get_int_or(
            spatial_reference_json,
            spatial_reference_json.HasMember("latestWkid") ? "latestWkid" : "wkid", -1));
        auto srid_descriptor = fmt::format("EPSG:{}", srid);

        if (srid != 3857 && srid != 4326)
        {
            pl_Crs crs;
            bool convert_success = hrz::convert_crs(
                srid_descriptor.c_str(), hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
            if (!convert_success)
            {
                return false;
            }
        }

        auto& origin_json = hrz::json::get_member_or_null(tile_info_json, "origin");
        if (origin_json.IsNull())
        {
            return false;
        }

        lm::dvec2 origin;
        origin.x =
            hrz::json::get_double_or(origin_json, "x", std::numeric_limits<double>::quiet_NaN());
        origin.y =
            hrz::json::get_double_or(origin_json, "y", std::numeric_limits<double>::quiet_NaN());

        if (std::isnan(origin.x) || std::isnan(origin.y))
        {
            return false;
        }

        auto& lods_json = hrz::json::get_member_or_null(tile_info_json, "lods");
        if (lods_json.IsNull())
        {
            return false;
        }

        std::map<uint32_t, double> source_lods;
        std::map<uint32_t, const char*> source_lod_names;

        for (auto& lod : lods_json.GetArray())
        {
            int level = hrz::json::get_int_or(lod, "level", -1);
            if (level < (int)declared_min_lod || level > (int)declared_max_lod) continue;

            double resolution = hrz::json::get_double_or(
                lod, "resolution", std::numeric_limits<double>::quiet_NaN());
            if (std::isnan(resolution)) continue;

            source_lods[level] = resolution;

            auto lod_name = hrz::json::get_str_or(lod, "levelValue", nullptr);
            if (lod_name != nullptr)
            {
                source_lod_names[level] = lod_name;
            }
        }

        if (source_lods.empty())
        {
            return false;
        }

        uint32_t min_lod = 0;
        double min_lod_resolution = 0;

        bool is_first_lod = true;
        uint32_t previous_lod = 0;
        double previous_resolution = 0;

        std::vector<std::string> lod_names;

        for (auto& it : source_lods)
        {
            auto lod = it.first;
            auto resolution = it.second;

            if (is_first_lod)
            {
                min_lod = lod;
                min_lod_resolution = resolution;
                is_first_lod = false;
            }
            else
            {
                if (lod != previous_lod + 1
                    || !hrz::flt_near(previous_resolution / resolution, 2.0, 0.0001))
                {
                    // The LODs either have a gap, or do not have a regular
                    // zoom factor of 2 between a level and the next.
                    return false;
                }
            }

            auto name_it = source_lod_names.find(lod);
            if (name_it == source_lod_names.end())
            {
                lod_names.push_back(std::to_string(lod));
            }
            else
            {
                lod_names.push_back({name_it->second});
            }

            previous_lod = lod;
            previous_resolution = resolution;
        }

        uint32_t max_lod = previous_lod;

        uint64_t tile_count_at_min_lod = (uint64_t)1 << min_lod;
        uint64_t tile_count_at_max_lod = (uint64_t)1 << max_lod;
        lm::dvec2 bounds_max = {
            origin.x + tile_count_at_min_lod * tile_size * min_lod_resolution,
            origin.y - tile_count_at_min_lod * tile_size * min_lod_resolution,
        };

        std::optional<lm::dbbox2> full_extent = std::nullopt;
        if (!full_extent_json.IsNull())
        {
            double nan = std::numeric_limits<double>::quiet_NaN();
            double x_min = hrz::json::get_double_or(full_extent_json, "xmin", nan);
            double y_min = hrz::json::get_double_or(full_extent_json, "ymin", nan);
            double x_max = hrz::json::get_double_or(full_extent_json, "xmax", nan);
            double y_max = hrz::json::get_double_or(full_extent_json, "ymax", nan);

            if (!std::isnan(x_min) && !std::isnan(y_min) && !std::isnan(x_max) && !std::isnan(y_max)
                && x_max > x_min && y_max > y_min)
            {
                full_extent = lm::dbbox2{{x_min, y_min}, {x_max, y_max}};
            }
        }

        if (srid == 3857 && hrz::flt_near(origin.x, -hrz::HALF_MERCATOR_RANGE, 1.0)
            && hrz::flt_near(origin.y, hrz::HALF_MERCATOR_RANGE, 1.0))
        {
            // This is a web Mercator global tiling scheme.

            geometry.projection.set_descriptor_type(
                hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
            geometry.projection.set_descriptor_(hrz_proj::wmerc_proj_str);

            geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::GLOBAL);
            auto tiling = geometry.tiling_scheme.mutable_global_tiling();
            tiling->set_tile_size(tile_size);
            tiling->set_level_zero_tile_count_x(1);
            tiling->set_level_zero_tile_count_y(1);
            tiling->set_min_level(min_lod);
            tiling->set_max_level(max_lod);
            tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
        }
        if (srid == 4326 && hrz::flt_near(origin.x, -180.0, 0.0001)
            && hrz::flt_near(origin.y, 90.0, 0.0001))
        {
            // This is a lat-long global tiling scheme.

            geometry.projection.set_descriptor_type(
                hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
            geometry.projection.set_descriptor_(hrz_proj::lonlat_deg_proj_str);

            geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::GLOBAL);
            auto tiling = geometry.tiling_scheme.mutable_global_tiling();
            tiling->set_tile_size(tile_size);
            tiling->set_level_zero_tile_count_x(2);
            tiling->set_level_zero_tile_count_y(1);
            tiling->set_min_level(min_lod);
            tiling->set_max_level(max_lod);
            tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
        }
        else
        {
            // This is not a well-known tiling scheme, so fallback to a local tiling scheme.

            // Cannot setup a local tiling scheme while ignoring extent.
            if (ignore_extent) return false;

            uint32_t lod_offset_correction = 0;

            if (full_extent.has_value() && full_extent->min.x >= origin.x
                && full_extent->max.y <= origin.y)
            {
                uint32_t required_tile_count = std::max(
                    hrz::next_power_of_two((uint32_t)std::ceil(
                        (full_extent->max.x - origin.x) / (tile_size * min_lod_resolution))),
                    hrz::next_power_of_two((uint32_t)std::ceil(
                        (full_extent->max.x - origin.x) / (tile_size * min_lod_resolution))));
                if (required_tile_count > tile_count_at_min_lod)
                {
                    lod_offset_correction = hrz::log2(required_tile_count) - min_lod;

                    bounds_max = {
                        origin.x + required_tile_count * tile_size * min_lod_resolution,
                        origin.y - required_tile_count * tile_size * min_lod_resolution,
                    };
                }
            }

            uint64_t full_image_size = tile_size * tile_count_at_max_lod;

            geometry.projection.set_descriptor_type(
                HrzProtocol::SrsDescriptorType::SRID_DESCRIPTOR);
            geometry.projection.set_descriptor_(srid_descriptor);

            geometry.tiling_scheme.set_type(HrzProtocol::TilingSchemeType::LOCAL);
            auto tiling = geometry.tiling_scheme.mutable_local_tiling();
            tiling->set_full_image_width(full_image_size);
            tiling->set_full_image_height(full_image_size);
            tiling->set_tile_size(tile_size);
            tiling->set_has_min_level(true);
            tiling->set_min_level(min_lod);
            tiling->set_has_max_level(true);
            tiling->set_max_level(max_lod);
            tiling->set_override_level_offset(true);
            tiling->set_level_offset(std::ceil(std::log2(tile_size)) + lod_offset_correction);
            tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
            tiling->set_tiling_origin(hrz_proto::TilingOrigin::TOP_ORIGIN);

            geometry.projection_bounds.min.x = origin.x;
            geometry.projection_bounds.min.y = bounds_max.y;
            geometry.projection_bounds.max.x = bounds_max.x;
            geometry.projection_bounds.max.y = origin.y;
        }

        if (ignore_extent)
        {
            geometry.bounds = lm::dbbox2{};
        }
        else if (full_extent.has_value())
        {
            geometry.bounds = full_extent.value();
        }
        else
        {
            geometry.bounds.min.x = origin.x;
            geometry.bounds.min.y = bounds_max.y;
            geometry.bounds.max.x = bounds_max.x;
            geometry.bounds.max.y = origin.y;
        }

        auto url_generator = std::make_unique<::TileUrlGenerator>(
            hrz::BaseUrl{url, preserve_query_parameters}, std::move(lod_names), min_lod);

        fetcher.emplace(
            std::make_unique<UrlTileRequester>(std::move(url_generator), std::nullopt, headers),
            min_lod, missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
            std::make_unique<ImageTileDecoder>(get_image_format(), raster_id),
            std::make_unique<SimpleTileAttributionPolicy>(attribution), tile_cache_capacity,
            raster_id, TileFetcher::MetricInfo{"ArcGIS", url.c_str()});

        return true;
    }

    bool setup_provider(AttributionRegistry* attributions, std::span<const std::byte> raw_data)
    {
        HRZ_SCOPED_SAMPLE("parse arcgis json data");

        rapidjson::Document doc;
        doc.Parse((const char*)raw_data.data(), raw_data.size());

        std::string format = "";

        if (doc.HasParseError())
        {
            HRZ_LOG_INFO("Couldn't parse ArcGIS MapServer response");
            return false;
        }

        AttributionHandle attribution_handles[] = {
            attribution::register_attribution(attributions, {additional_attribution, ""}),
            attribution::register_attribution(
                attributions, {hrz::json::get_str_or(doc, "copyrightText", ""), ""})};
        auto attribution =
            attribution::register_attribution_group(attributions, attribution_handles);

        if (layer_ids.empty() && doc.HasMember("tileInfo"))
        {
            uint32_t min_lod = hrz::clamp(hrz::json::get_int_or(doc, "minLOD", 0), 0, 255);
            uint32_t max_lod = hrz::clamp(hrz::json::get_int_or(doc, "maxLOD", 255), 0, 255);

            if (parse_tile_info(
                    doc["tileInfo"], hrz::json::get_member_or_null(doc, "fullExtent"), min_lod,
                    max_lod, attribution))
            {
                return true;
            }
        }

        double min_scale = 0, max_scale = 0;

        if (doc.HasMember("layers") && doc["layers"].IsArray())
        {
            auto& layers = doc["layers"];
            bool found_one = false;
            for (auto layer_id : layer_ids)
            {
                const auto& layer_array = layers.GetArray();

                if (layer_id < (int)layer_array.Size())
                {
                    auto& layer = layer_array[layer_id];
                    double layer_min_scale = layer["minScale"].GetDouble();
                    if (layer_min_scale != 0)
                    {
                        min_scale =
                            min_scale == 0 ? layer_min_scale : std::min(min_scale, layer_min_scale);
                    }
                    double layer_max_scale = layer["maxScale"].GetDouble();
                    if (layer_max_scale != 0)
                    {
                        max_scale =
                            max_scale == 0 ? layer_max_scale : std::max(max_scale, layer_max_scale);
                    }

                    found_one = true;
                }
            }

            if (!layer_ids.empty() && !found_one)
            {
                HRZ_LOG_INFO("Requested ArcGIS layers not found, displaying all layers instead");
            }
        }

        std::optional<size_t> best_format_index = std::nullopt;
        std::optional<size_t> desired_format_index = std::nullopt;

        if (doc.HasMember("supportedImageFormatTypes"))
        {
            // The descriptor contains a list of formats. Try to find one in the list
            // that is supported.
            // If the user has specified a format, verify that it's in the list.

            std::string_view image_formats_str =
                hrz::json::get_str_or(doc, "supportedImageFormatTypes", "");
            int next_comma = -1;
            do
            {
                next_comma = hrz::str::find(image_formats_str, ',');

                std::string_view image_format = image_formats_str.substr(
                    0, next_comma != -1 ? next_comma : image_formats_str.size());

                for (size_t i = 0; i < HRZ_ARRAY_COUNT(IMAGE_FORMATS); ++i)
                {
                    // The "supportedImageFormatTypes" field usually contains uppercase
                    // format names.
                    // The map service takes lowercase format names, according to the
                    // documentation. Although it seems to accept both.
                    // Likewise, we don't take case into account when matching formats
                    // names.
                    if (hrz::str::iequals(image_format, IMAGE_FORMATS[i]))
                    {
                        if (!desired_format_index.has_value() && !desired_image_format.empty()
                            && hrz::str::iequals(image_format, desired_image_format))
                        {
                            desired_format_index = {i};
                        }

                        if (best_format_index.has_value())
                        {
                            best_format_index = {std::min(best_format_index.value(), i)};
                        }
                        else
                        {
                            best_format_index = {i};
                        }
                        break;
                    }
                }

                if (next_comma != -1)
                {
                    image_formats_str = image_formats_str.substr(next_comma + 1);
                }
            } while (next_comma != -1);

            if (!desired_image_format.empty())
            {
                if (desired_format_index.has_value())
                {
                    format = IMAGE_FORMATS[desired_format_index.value()];
                }
                else
                {
                    HRZ_LOG_ERROR("Unavailable image format: {}", desired_image_format);
                    return false;
                }
            }
            else if (best_format_index.has_value())
            {
                format = IMAGE_FORMATS[best_format_index.value()];
            }

            if (format.empty())
            {
                HRZ_LOG_ERROR("No supported image format available");
                return false;
            }
        }
        else
        {
            // No "supportedImageFormatTypes" field. We use the format
            // the user has specified, if it is supported.
            if (!desired_image_format.empty())
            {
                for (size_t i = 0; i < HRZ_ARRAY_COUNT(IMAGE_FORMATS); ++i)
                {
                    if (hrz::str::iequals(IMAGE_FORMATS[i], desired_image_format))
                    {
                        format = IMAGE_FORMATS[i];
                    }
                }

                if (format.empty())
                {
                    HRZ_LOG_ERROR("Unsupported image format: {}", desired_image_format);
                }
            }
            else
            {
                // Assume PNG is always available.
                // "jpgpng" would be a better choice, but it isn't available on ArcGIS 9.
                format = "png";
            }
        }

        int srid = -1;
        if (doc.HasMember("spatialReference"))
        {
            auto& spatial_reference_json = doc["spatialReference"];
            srid = canonicalize_srid(hrz::json::get_int_or(
                spatial_reference_json,
                spatial_reference_json.HasMember("latestWkid") ? "latestWkid" : "wkid", -1));
        }
        if (srid != 3857 && srid != 4326)
        {
            srid = 3857;
        }

        geometry.projection.set_descriptor_type(HrzProtocol::SrsDescriptorType::SRID_DESCRIPTOR);
        geometry.projection.set_descriptor_(fmt::format("EPSG:{}", srid));

        std::optional<Extent> full_extent = std::nullopt;
        if (doc.HasMember("fullExtent"))
        {
            full_extent = get_extent(doc["fullExtent"]);
        }

        std::optional<Extent> initial_extent = std::nullopt;
        if (doc.HasMember("initialExtent"))
        {
            initial_extent = get_extent(doc["initialExtent"]);
        }

        Extent extent;
        if (full_extent.has_value())
        {
            extent = full_extent.value();

            if (initial_extent.has_value())
            {
                if (initial_extent->srid == full_extent->srid)
                {
                    // Some layers are misconfigured and their extents are wrong.
                    // If the initial extent is outside the full extent, we merge
                    // the two to maximise the chances of covering the actual map
                    // extent.
                    extent.bounds = lm::merge(full_extent->bounds, initial_extent->bounds);
                }
            }
        }
        else if (initial_extent.has_value())
        {
            extent = initial_extent.value();
        }
        else
        {
            HRZ_LOG_WARNING("No extents are defined.");
            extent.srid = 3857;
            extent.bounds = lm::dbbox2{{0, 0}, {0, 0}};
        }

        if (extent.srid != (uint32_t)srid)
        {
            extent = reproject_extent(extent, srid);
        }

        geometry.projection.set_descriptor_type(HrzProtocol::SrsDescriptorType::SRID_DESCRIPTOR);
        geometry.projection.set_descriptor_(fmt::format("EPSG:{}", srid));

        if (!ignore_extent)
        {
            geometry.bounds = extent.bounds;
        }
        else
        {
            geometry.bounds = lm::dbbox2{};
        }

        int max_image_width = hrz::json::get_int_or(doc, "maxImageWidth", 256);
        int max_image_height = hrz::json::get_int_or(doc, "maxImageHeight", 256);
        if (max_image_width <= 0 || max_image_height <= 0)
        {
            HRZ_LOG_ERROR("Invalid max image width or height");
            return false;
        }
        uint32_t tile_size = (uint32_t)std::min(256, std::min(max_image_width, max_image_height));

        geometry.tiling_scheme.set_type(HrzProtocol::TilingSchemeType::GLOBAL);
        auto* tiling_scheme = geometry.tiling_scheme.mutable_global_tiling();
        tiling_scheme->set_tile_size(tile_size);
        tiling_scheme->set_border_tile_aspect(HrzProtocol::BorderTileAspect::FULL_SIZED);

        tiling_scheme->set_level_zero_tile_count_x(srid == 4326 ? 2 : 1);
        tiling_scheme->set_level_zero_tile_count_y(1);

        // Scales explanation for 3857 :
        // https://developers.arcgis.com/documentation/mapping-apis-and-services/reference/zoom-levels-and-scale/
        // For 4326 it seems that it is the same approach.
        if (min_scale == 0)
        {
            tiling_scheme->set_min_level(0);
        }
        else
        {
            double lod_min;
            if (srid == 4326)
            {
                lod_min = std::ceil(
                    std::log2(hrz::EARTH_CIRCUMFERENCE / (min_scale * pixel_size * tile_size)));
                lod_min -= 1;
            }
            else
            {
                lod_min = std::ceil(
                    std::log2(hrz::MERCATOR_RANGE / (min_scale * pixel_size * tile_size)));
            }
            tiling_scheme->set_max_level(lod_min);
        }

        if (max_scale == 0)
        {
            tiling_scheme->set_max_level(23);
        }
        else
        {
            double lod_max;
            if (srid == 4326)
            {
                lod_max =
                    std::log2(hrz::EARTH_CIRCUMFERENCE / (max_scale * pixel_size * tile_size));
                lod_max -= 1;
            }
            else
            {
                lod_max = std::log2(hrz::MERCATOR_RANGE / (max_scale * pixel_size * tile_size));
            }
            tiling_scheme->set_max_level(lod_max);
        }

        auto capabilities_opt = hrz::json::get_str(doc, "capabilities");
        if (!capabilities_opt.has_value()) return false;

        bool has_map_operation = false;
        bool has_image_operation = false;
        {
            std::string_view capabilities_str = capabilities_opt.value();
            int next_comma = -1;
            do
            {
                next_comma = hrz::str::find(capabilities_str, ',');

                std::string_view capability = capabilities_str.substr(
                    0, next_comma != -1 ? next_comma : capabilities_str.size());

                if (capability == "Map")
                {
                    has_map_operation = true;
                }
                else if (capability == "Image")
                {
                    has_image_operation = true;
                }

                if (next_comma != -1)
                {
                    capabilities_str = capabilities_str.substr(next_comma + 1);
                }
            } while (next_comma != -1);
        }

        if (has_map_operation)
        {
            auto url_generator = std::make_unique<ExportMapUrlGenerator>(
                hrz::BaseUrl{url, preserve_query_parameters}, srid, tile_size, format,
                format_supports_transparency(format), layer_ids);

            fetcher.emplace(
                std::make_unique<UrlTileRequester>(std::move(url_generator), std::nullopt, headers),
                0, missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
                std::make_unique<ImageTileDecoder>(get_image_format(), raster_id),
                std::make_unique<SimpleTileAttributionPolicy>(attribution), tile_cache_capacity,
                raster_id,
                TileFetcher::MetricInfo{
                    provider_request_tally_metric_name(
                        hrz_proto::RasterProviderType::ARCGIS_RASTER_PROVIDER),
                    url.c_str()});
            return true;
        }
        else if (has_image_operation)
        {
            auto url_generator = std::make_unique<ExportImageUrlGenerator>();
            url_generator->srid = srid;
            url_generator->base_url = url;
            url_generator->tile_size = tile_size;
            url_generator->format = format;

            fetcher.emplace(
                std::make_unique<UrlTileRequester>(std::move(url_generator), std::nullopt, headers),
                0, missing_tile_policy == hrz_proto::MissingTilePolicy::USE_LOWER_RESOLUTION,
                std::make_unique<ImageTileDecoder>(get_image_format(), raster_id),
                std::make_unique<SimpleTileAttributionPolicy>(attribution), tile_cache_capacity,
                raster_id, TileFetcher::MetricInfo{"ArcGIS", url.c_str()});
            return true;
        }

        HRZ_LOG_ERROR("Insufficient capabilities for tile querying");

        return false;
    }

public:
    void work(
        AssetsLoader* al,
        BlobAllocator* ba,
        JobScheduler* js,
        AttributionRegistry* attributions) override
    {
        assert(al && ba && js);

        if (status == InternalStatus::LoadingAndParsingDescriptor)
        {
            if (!assets_loader::is_valid(al, download_ticket))
            {
                download_ticket =
                    assets_loader::begin(al, hrz::url::join({url, "?f=json"}), headers, al_queue);
            }
            else if (assets_loader::is_finished(al, download_ticket))
            {
                if (assets_loader::get_status(al, download_ticket)
                    == assets_loader::RequestStatus::Loaded)
                {
                    auto raw_data = assets_loader::get_blob(al, ba, download_ticket).get_data();
                    bool success = setup_provider(attributions, raw_data);
                    if (success)
                    {
                        status = InternalStatus::Ready;
                    }
                    else
                    {
                        HRZ_LOG_ERROR("Couldn't setup ArcGIS provider at {}", url);
                        status = InternalStatus::Error;
                    }
                }
                else
                {
                    HRZ_LOG_ERROR("Couldn't load ArcGIS resource at {}", url);
                    status = InternalStatus::Error;
                }

                assets_loader::end(al, download_ticket);
            }
        }
        else if (status == InternalStatus::Ready)
        {
            fetcher->work(al, ba, js, attributions);
        }
    }

    bool is_working() const override
    {
        return status == InternalStatus::LoadingAndParsingDescriptor
            || (status == InternalStatus::Ready && fetcher->is_working());
    }

    UpdateAction notify_model_update(
        const scene_model::RasterProviderPath& path,
        const hrz_proto::RasterProvider& provider_model) override
    {
        if (path.leaf() || path.is_type())
        {
            return UpdateAction::RecreateProvider;
        }
        else if (path.is_arcgis())
        {
            auto provider_path = path.clone().arcgis();
            if (provider_path.is_http_headers())
            {
                if (set_http_headers(
                        assets_loader::from_proto(provider_model.arcgis().http_headers())))
                {
                    return UpdateAction::RecreateProvider;
                }
                else
                {
                    return UpdateAction::KeepTiles;
                }
            }
            else
            {
                return UpdateAction::RecreateProvider;
            }
        }
        else
        {
            return UpdateAction::KeepTiles;
        }
    }

    bool set_http_headers(const HttpHeaders& new_headers) override
    {
        auto old = std::exchange(headers, new_headers);
        if (fetcher.has_value())
        {
            fetcher->set_http_headers(headers);
        }
        return old.hash_content() != headers.hash_content();
    }

    void dev_ui(mu_Context* ctx) override
    {
        fmt::memory_buffer buffer;

        fmt::format_to(std::back_inserter(buffer), "ArcGISProvider [{}]", url);
        buffer.push_back(0);

        if (mu_begin_treenode(ctx, buffer.data()))
        {
            if (fetcher.has_value())
            {
                fetcher->dev_ui(ctx);
            }
            mu_end_treenode(ctx);
        }
    }

private:
    enum class InternalStatus
    {
        LoadingAndParsingDescriptor,
        Ready,
        Error,
    };

    InternalStatus status;
    std::string url;
    HttpHeaders headers;
    std::string additional_attribution;
    bool preserve_query_parameters;
    assets_loader::Queue al_queue;
    hrz_proto::MissingTilePolicy missing_tile_policy;
    std::string desired_image_format;
    std::vector<int> layer_ids;
    bool ignore_extent;
    size_t tile_cache_capacity;

    assets_loader::Ticket download_ticket;

    std::optional<TileFetcher> fetcher;
    hrz::planet::TiledRasterGeometry geometry;

    uint64_t raster_id;
};

std::unique_ptr<RasterProvider> create_arcgis_provider(
    const hrz_proto::ArcGisRasterProviderParams& params,
    assets_loader::Queue queue,
    uint32_t default_tile_cache_size,
    uint64_t raster_id)
{
    return std::unique_ptr<RasterProvider>(
        new ArcGisProvider(params, queue, default_tile_cache_size, raster_id));
}

} // namespace hrz::planet
