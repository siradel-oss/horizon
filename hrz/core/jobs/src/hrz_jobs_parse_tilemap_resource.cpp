#include "hrz_jobs_declarations.h"

#include <hrz_common_crs_database.h>
#include <hrz_common_geo.h>
#include <hrz_common_planet.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_string_utils.h>

#include <pugixml/pugixml.hpp>

// See https://wiki.osgeo.org/wiki/Tile_Map_Service_Specification

namespace
{
enum class Profile
{
    Unknown,
    Mercator,
    Geodetic,
    Local,
    Raster,
};
} // namespace

namespace hrz_jobs::parse_tilemap_resource
{
hrz::JobResult run(
    const hrz::planet::TilemapResourceParams& params,
    hrz::planet::TilemapResourceResponse& response,
    const JobContext&)
{
    HRZ_SCOPED_SAMPLE("parse tilemapresource xml data");

    auto raw_xml = params.raw_xml.get_data();

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_buffer(raw_xml.data(), raw_xml.size());

    raw_xml.release();

    if (parse_result.status != pugi::status_ok)
    {
        HRZ_LOG_ERROR("Couldn't parse tilemapresource.xml: {}", parse_result.description());
        return hrz::JobResult::FAILURE;
    }

    const pugi::xml_node root = doc.child("TileMap");
    const pugi::xml_node bbox_node = root.child("BoundingBox");
    const pugi::xml_node tilesets_node = root.child("TileSets");
    const pugi::xml_node srs_node = root.child("SRS");
    const pugi::xml_node tile_format_node = root.child("TileFormat");

    std::string profile_str = tilesets_node.attribute("profile").value();
    std::string srs = srs_node.text().get();

    const unsigned int tile_width = tile_format_node.attribute("width").as_uint();
    const unsigned int tile_height = tile_format_node.attribute("height").as_uint();

    if (tile_width != tile_height)
    {
        HRZ_LOG_ERROR("Tiles must be square");
        return hrz::JobResult::FAILURE;
    }

    lm::dbbox2 bounds = {
        {
            bbox_node.attribute("minx").as_double(),
            bbox_node.attribute("miny").as_double(),
        },
        {
            bbox_node.attribute("maxx").as_double(),
            bbox_node.attribute("maxy").as_double(),
        },
    };

    auto check_srs = [&]()
    {
        if (!hrz::convert_crs(srs.c_str(), response.geometry.mutable_projection()))
        {
            // Gdal2tiles writes the SRS in WKT, and this is not supported.
            // It has to be manually changed to a PROJ.4 string.
            // @Todo Support WKT SRS definitions?
            HRZ_LOG_ERROR("tilemapresource.xml SRS \"{}\" is unknown or invalid", srs);
            return false;
        }
        return true;
    };

    Profile profile = Profile::Unknown;
    // Despite TMS specifications telling to use the profile 'global-mercator',
    // gdal2tiles sets the profile to 'mercator'. (Same for 'geodetic'.)
    // What a great idea!
    // Gdal2tiles also has a `raster` profile, which uses local tiling, but is
    // a bit different from `local`.
    if (profile_str == "mercator" || profile_str == "global-mercator")
    {
        // We're being generous here, the spec says that in this case the
        // SRS must be OSGEO:41001.
        if (srs != "OSGEO:41001" && srs != "EPSG:3857")
        {
            HRZ_LOG_ERROR("SRS must be \"OSGEO:41001\" or \"EPSG:3857\" for mercator profile");
            return hrz::JobResult::FAILURE;
        }

        // Coordinates seem to (always?) be given in lat/lon for the mercator profile.
        // At least when generated with gdal2tiles.
        bounds.min = hrz::geo_to_web_mercator(
            hrz::GeoPosition2{lm::radians(bounds.min.y), lm::radians(bounds.min.x)});
        bounds.max = hrz::geo_to_web_mercator(
            hrz::GeoPosition2{lm::radians(bounds.max.y), lm::radians(bounds.max.x)});

        profile = Profile::Mercator;
    }
    else if (profile_str == "geodetic" || profile_str == "global-geodetic")
    {
        if (srs != "EPSG:4326")
        {
            HRZ_LOG_ERROR("SRS must be \"EPSG:4326\" for geodetic profile");
            return hrz::JobResult::FAILURE;
        }

        profile = Profile::Geodetic;
    }
    else if (profile_str == "local")
    {
        profile = Profile::Local;
    }
    else if (profile_str == "raster")
    {
        profile = Profile::Raster;
    }
    else
    {
        HRZ_LOG_ERROR("Unknown TileMapService '{}' profile", profile_str);
        return hrz::JobResult::FAILURE;
    }

    if (!check_srs()) return hrz::JobResult::FAILURE;

    uint8_t min_level = std::numeric_limits<uint8_t>::max();
    uint8_t max_level = std::numeric_limits<uint8_t>::min();
    double max_level_units_per_pixel = 0;
    for (auto it = tilesets_node.begin(); it != tilesets_node.end(); ++it)
    {
        const uint8_t level = (uint8_t)it->attribute("order").as_uint();
        min_level = std::min(min_level, level);
        max_level = std::max(max_level, level);

        if (level == max_level)
        {
            max_level_units_per_pixel = (double)it->attribute("units-per-pixel").as_double();
        }
    }

    // Note on Mercator profile:
    // The specification says that this equation must be true:
    //             units-per-pixel = 78271.516 / 2^n
    // However in practice, most tilemapresource.xml tilesets follow this equation:
    //             units-per-pixel = 78271.516 / 2^(n - 1)
    // or rather:
    //             units-per-pixel = 156543.032 / 2^n
    // This matches nicely with the usual Mercator tilesets, with level 0 comprising
    // only one tile, covering the whole planet (minus the poles).
    //
    // We could detect which level 0 convention (as it boils down to this) is used,
    // and act accordingly. But for this we would need to be able to tell the raster
    // provider to shift the levels in the requests, which we cannot do at the moment.
    // Let's wait until the need arises.
    //     -tpetillon, 2020-06-12

    auto* tiling_scheme = response.geometry.mutable_tiling_scheme();

    if (profile == Profile::Mercator)
    {
        tiling_scheme->set_type(hrz_proto::TilingSchemeType::GLOBAL);

        auto* global_tiling = tiling_scheme->mutable_global_tiling();
        global_tiling->set_tile_size(tile_width);
        global_tiling->set_level_zero_tile_count_x(1);
        global_tiling->set_level_zero_tile_count_y(1);
        global_tiling->set_min_level(min_level);
        global_tiling->set_max_level(max_level);
        global_tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
    }
    else if (profile == Profile::Geodetic)
    {
        tiling_scheme->set_type(hrz_proto::TilingSchemeType::GLOBAL);

        auto* global_tiling = tiling_scheme->mutable_global_tiling();
        global_tiling->set_tile_size(tile_width);
        global_tiling->set_level_zero_tile_count_x(2);
        global_tiling->set_level_zero_tile_count_y(1);
        global_tiling->set_min_level(min_level);
        global_tiling->set_max_level(max_level);
        global_tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
    }
    else if (profile == Profile::Local)
    {
        // @Todo Is this actually supported?

        tiling_scheme->set_type(hrz_proto::TilingSchemeType::LOCAL);

        auto* local_tiling = tiling_scheme->mutable_local_tiling();
        local_tiling->set_full_image_width(((uint64_t)1 << max_level) * tile_width);
        local_tiling->set_full_image_height(((uint64_t)1 << max_level) * tile_height);
        local_tiling->set_tile_size(tile_width);
        local_tiling->set_min_level(min_level);
        local_tiling->set_has_min_level(true);
        local_tiling->set_max_level(max_level);
        local_tiling->set_has_max_level(true);
        local_tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
        local_tiling->set_tiling_origin(hrz_proto::TilingOrigin::BOTTOM_ORIGIN);
    }
    else if (profile == Profile::Raster)
    {
        const lm::dvec2 bounds_size = lm::size(bounds);

        uint64_t image_width = std::round(bounds_size.x / max_level_units_per_pixel);
        uint64_t image_height = std::round(bounds_size.y / max_level_units_per_pixel);

        tiling_scheme->set_type(hrz_proto::TilingSchemeType::LOCAL);

        auto* local_tiling = tiling_scheme->mutable_local_tiling();
        local_tiling->set_full_image_width(image_width);
        local_tiling->set_full_image_height(image_height);
        local_tiling->set_tile_size(tile_width);
        local_tiling->set_min_level(min_level);
        local_tiling->set_has_min_level(true);
        local_tiling->set_max_level(max_level);
        local_tiling->set_has_max_level(true);
        local_tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
        local_tiling->set_tiling_origin(hrz_proto::TilingOrigin::BOTTOM_ORIGIN);
    }
    else
    {
        HRZ_LOG_ERROR("Unknown TileMapService profile");
        return hrz::JobResult::FAILURE;
    }

    auto* response_bounds = response.geometry.mutable_bounds();
    response_bounds->set_x_min(bounds.min.x);
    response_bounds->set_y_min(bounds.min.y);
    response_bounds->set_x_max(bounds.max.x);
    response_bounds->set_y_max(bounds.max.y);

    // It is possible to create tilesets with the XYZ tiling scheme using gdal2tiles.
    // However, their tilemapresource.xml is strictly identical to those of TMS tilesets.
    // They are not spec-conforming, and there is no way to distinguish them, so we have
    // to choose one or the other. :/
    // Here the TMS spec is followed.
    bool reverse_y = profile == Profile::Mercator || profile == Profile::Geodetic;

    for (size_t i = 0; i <= max_level; ++i)
    {
        response.url_patterns.push_back("");
    }

    for (const auto& tileset_node : tilesets_node.children("TileSet"))
    {
        uint32_t order = tileset_node.attribute("order").as_uint(max_level + 1);

        if (order < min_level || order > max_level) continue;

        std::string href = tileset_node.attribute("href").value();
        if (hrz::str::starts_with(href, "http"))
        {
            response.url_patterns.at(order) = fmt::format(
                "{base_url}/{{x}}/{y}.{ext}", fmt::arg("base_url", href),
                fmt::arg("y", reverse_y ? "{ry}" : "{y}"),
                fmt::arg("ext", tile_format_node.attribute("extension").value()));
        }
        else
        {
            // Specs says "The value of an "href" must be an absolute URL (starting with
            // "http://")". But sometimes tilemapresource.xml root is assumed as absolute URL
            // and then only relatives hrefs are used *sigh*.

            response.url_patterns.at(order) = fmt::format(
                "{z}/{{x}}/{y}.{ext}", fmt::arg("z", href),
                fmt::arg("y", reverse_y ? "{ry}" : "{y}"),
                fmt::arg("ext", tile_format_node.attribute("extension").value()));
        }
    }

    if (response.url_patterns.empty())
    {
        HRZ_LOG_ERROR("No tileset URLs found");
        return hrz::JobResult::FAILURE;
    }

    auto attribution_node = root.child("Attribution");
    response.attribution_title = attribution_node.child("Title").text().as_string();
    response.attribution_logo = attribution_node.child("Logo").attribute("href").as_string();

    return hrz::JobResult::SUCCESS;
}

} // namespace hrz_jobs::parse_tilemap_resource
