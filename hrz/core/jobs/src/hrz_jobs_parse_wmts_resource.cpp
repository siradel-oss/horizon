#include "hrz_jobs_declarations.h"
#include "hrz_jobs_ogc_utils.h"

#include <hrz_common_crs_database.h>
#include <hrz_common_planet.h>
#include <hrz_common_proj.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_string_utils.h>
#include <hrz_fnd_url_utils.h>
#include <hrz_protocol_all.h>

#include <fmt/format.h>
#include <lin_maths.h>
#include <proj_lite.h>
#include <pugixml/pugixml.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

// See https://www.ogc.org/standards/wmts

namespace
{
lm::dvec2 parse_dvec2(std::string_view text)
{
    lm::dvec2 v;
    char* end;
    v.x = strtod(text.data(), &end);
    v.y = strtod(end, nullptr);
    return v;
}

lm::dbbox2 parse_bounding_box(const pugi::xml_node& bbox_node)
{
    lm::dbbox2 bbox = lm::dbbox2::invalid();
    bbox = lm::expand(bbox, parse_dvec2(bbox_node.child_value("ows:LowerCorner")));
    bbox = lm::expand(bbox, parse_dvec2(bbox_node.child_value("ows:UpperCorner")));
    return bbox;
}

std::optional<lm::dbbox2> transform_bbox(const lm::dbbox2& bbox, const pl_Transform& transform)
{
    lm::dvec3 min = {bbox.min, 0};
    auto min_res = pl_transform_in_place_canonical(&transform, 1, &min.x);

    lm::dvec3 max = {bbox.max, 0};
    auto max_res = pl_transform_in_place_canonical(&transform, 1, &max.x);

    if (min_res != pl_Result_Ok || max_res != pl_Result_Ok)
    {
        HRZ_LOG_WARNING("Couldn't project bounding box");
        return std::nullopt;
    }

    auto transformed_bbox = lm::dbbox2::invalid();
    transformed_bbox = lm::expand(transformed_bbox, min.xy);
    transformed_bbox = lm::expand(transformed_bbox, max.xy);

    return {transformed_bbox};
}

lm::dbbox2 transform_and_intersect_bbox(
    const lm::dbbox2& bbox,
    const lm::dbbox2& wgs84_bbox,
    const char* srs)
{
    if (wgs84_bbox.min.x <= -180 && wgs84_bbox.min.y <= -90 && wgs84_bbox.max.x >= 180
        && wgs84_bbox.max.y >= 90)
    {
        return bbox;
    }

    pl_Crs crs;
    bool convert_success =
        hrz::convert_crs(srs, hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
    if (!convert_success)
    {
        HRZ_LOG_WARNING("Couldn't convert raster projection string to pl_Crs.");
        return bbox;
    }

    pl_Transform transform;
    auto transform_res = pl_bake_transform(&hrz_proj::lonlat_deg, &crs, &transform);
    if (transform_res != pl_Result_Ok)
    {
        HRZ_LOG_WARNING(
            "Couldn't create transform: {} ({})", pl_result_string(transform_res),
            fmt::underlying(transform_res));
        return bbox;
    }

    auto transformed_bbox = transform_bbox(wgs84_bbox, transform);
    if (!transformed_bbox.has_value())
    {
        return bbox;
    }

    return lm::intersection(bbox, transformed_bbox.value());
}

struct GetTileMethod
{
    hrz::planet::WmtsGetTileMethod method;
    std::string_view url;
};

// Only RESTful and KVP methods, combined with GET requests, are currently supported.
// The WMTS specification also includes SOAP and XML methods, as well as using POST
// requests. However their usage seem to be exceedingly rare. None of the public WMTS
// servers I have found required (or even supported) them.
//     -tpetillon, 2021-11-15
std::optional<GetTileMethod> find_get_tile_method(const pugi::xml_node& root_node)
{
    const auto& operations_metadata_node = root_node.child("ows:OperationsMetadata");
    if (!operations_metadata_node)
    {
        // This is what QGIS does.
        HRZ_LOG_WARNING("No operations metadata found, defaulting to RESTful GetTile method");
        return {{hrz::planet::WmtsGetTileMethod::GET_RESTFUL, ""}};
    }

    for (const auto& operation_node : operations_metadata_node.children("ows:Operation"))
    {
        const auto& dcp_node = operation_node.child("ows:DCP");
        if (!dcp_node) continue;

        const auto& http_node = dcp_node.child("ows:HTTP");
        if (!http_node) continue;

        const auto& get_node = http_node.child("ows:Get");
        if (!get_node) continue;

        const auto href = get_node.attribute("xlink:href").as_string("");
        if (std::strcmp(href, "") == 0) continue;

        for (const auto& constraint_node : get_node.children("ows:Constraint"))
        {
            const auto name = constraint_node.attribute("name").as_string("");
            if (std::strcmp(name, "GetEncoding") != 0) continue;

            const auto& allowed_values_node = constraint_node.child("ows:AllowedValues");
            if (!allowed_values_node) continue;

            for (const auto& value_node : allowed_values_node.children("ows:Value"))
            {
                const auto value = value_node.child_value();

                if (std::strcmp(value, "RESTful") == 0)
                {
                    return {{hrz::planet::WmtsGetTileMethod::GET_RESTFUL, href}};
                }
                else if (std::strcmp(value, "KVP") == 0)
                {
                    return {{hrz::planet::WmtsGetTileMethod::GET_KVP, href}};
                }
            }
        }
    }

    return std::nullopt;
}

struct TileMatrix
{
    const char* identifier;
    double scale;
    lm::dvec2 top_left_corner;
    lm::uvec2 tile_size;
    lm::uvec2 size;
};

struct TileMatrixSet
{
    bool google_maps_compatible;
    const char* crs_string;
    pl_Crs crs;
    lm::dbbox2 projection_bounds;
    lm::dbbox2 bounds;
    uint32_t tile_size;
    lm::uvec2 level_zero_tile_count;
    lm::uvec2 max_level_tile_count;
    const char* identifier;
    std::vector<const char*> matrix_identifiers;
};

bool is_global_web_mercator(const TileMatrixSet& matrix_set)
{
    if (matrix_set.google_maps_compatible) return true;

    // Check if the matrix set is compatible even if not explicitly marked.

    if (!pl_are_crs_equal(&matrix_set.crs, &hrz_proj::wmerc))
    {
        return false;
    }

    if (matrix_set.level_zero_tile_count != lm::uvec2(1, 1))
    {
        return false;
    }

    if (matrix_set.tile_size != 256)
    {
        return false;
    }

    static constexpr double eps = 1.0;
    if (std::abs(matrix_set.projection_bounds.min.x + hrz::HALF_MERCATOR_RANGE) > eps
        || std::abs(matrix_set.projection_bounds.min.y + hrz::HALF_MERCATOR_RANGE) > eps
        || std::abs(matrix_set.projection_bounds.max.x - hrz::HALF_MERCATOR_RANGE) > eps
        || std::abs(matrix_set.projection_bounds.max.y - hrz::HALF_MERCATOR_RANGE) > eps)
    {
        return false;
    }

    return true;
}

std::optional<TileMatrixSet> parse_tile_matrix_set(const pugi::xml_node& matrix_set_node)
{
    auto scale_set_node = matrix_set_node.child("WellKnownScaleSet");

    bool google_maps_compatible = scale_set_node
        && std::strcmp(
               scale_set_node.child_value(), "urn:ogc:def:wkss:OGC:1.0:GoogleMapsCompatible")
            == 0;

    auto crs_string = matrix_set_node.child_value("ows:SupportedCRS");

    // Fix an invalid OGC CRS string, that comes from a typo in the WMTS
    // specification (table E.4), that has been copied as-is by multiple
    // WMTS server implementations.
    // ("6.18:3" should be "6.18.3".)
    if (std::strcmp(crs_string, "urn:ogc:def:crs:EPSG:6.18:3:3857") == 0)
    {
        crs_string = "urn:ogc:def:crs:EPSG:6.18.3:3857";
    }

    pl_Crs crs;
    if (!hrz::convert_crs(crs_string, hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs))
    {
        // The projection isn't supported.
        return std::nullopt;
    }

    std::vector<TileMatrix> tile_matrices;

    for (const auto& matrix_node : matrix_set_node.children("TileMatrix"))
    {
        TileMatrix matrix;
        matrix.identifier = matrix_node.child_value("ows:Identifier");
        matrix.scale = matrix_node.child("ScaleDenominator").text().as_double();
        matrix.top_left_corner = parse_dvec2(matrix_node.child_value("TopLeftCorner"));
        matrix.tile_size.x = matrix_node.child("TileWidth").text().as_uint();
        matrix.tile_size.y = matrix_node.child("TileHeight").text().as_uint();
        matrix.size.x = matrix_node.child("MatrixWidth").text().as_uint();
        matrix.size.y = matrix_node.child("MatrixHeight").text().as_uint();

        if (matrix.tile_size.x != matrix.tile_size.y)
        {
            // Non-square tiles. This isn’t supported.
            return std::nullopt;
        }

        tile_matrices.push_back(matrix);
    }

    if (tile_matrices.empty()) return std::nullopt;

    std::sort(
        tile_matrices.begin(), tile_matrices.end(),
        [](const auto& a, const auto& b) { return a.scale > b.scale; });

    for (size_t i = 1; i < tile_matrices.size(); ++i)
    {
        const auto& matrix = tile_matrices.at(i);
        const auto& previous_matrix = tile_matrices.at(i - 1);

        double scale_ratio = previous_matrix.scale / matrix.scale;

        constexpr double eps = 0.000001;
        if (std::abs(scale_ratio - 2) > eps)
        {
            // The scale of one matrix set (i.e. zoom level) is not half
            // the scale of the previous one.
            // This isn’t supported.
            return std::nullopt;
        }

        if (matrix.tile_size != previous_matrix.tile_size)
        {
            // Irregular tile sizes. This isn’t supported.
            return std::nullopt;
        }

        if (matrix.top_left_corner != previous_matrix.top_left_corner)
        {
            // Non-aligned grid. This isn’t supported.
            return std::nullopt;
        }
    }

    const auto& first_matrix = tile_matrices.front();
    const auto& last_matrix = tile_matrices.back();

    double meters_per_pixel = 1;
    if (crs.params.type == pl_ProjectionType_LatLong)
    {
        meters_per_pixel = hrz::EARTH_RADIUS * crs.to_radian;
    }
    else
    {
        meters_per_pixel = crs.to_meter;
    }

    double tiling_width =
        (first_matrix.scale * hrz::ogc::PixelSize * first_matrix.size.x * first_matrix.tile_size.x)
        / meters_per_pixel;
    double tiling_height =
        (first_matrix.scale * hrz::ogc::PixelSize * first_matrix.size.y * first_matrix.tile_size.y)
        / meters_per_pixel;
    lm::dvec2 tiling_bottom_right_corner = {
        first_matrix.top_left_corner.x + tiling_width,
        first_matrix.top_left_corner.y - tiling_height // WMTS tile coords increase southwards.
    };
    lm::dbbox2 tiling_bounds = lm::dbbox2::invalid();
    tiling_bounds = lm::expand(tiling_bounds, first_matrix.top_left_corner);
    tiling_bounds = lm::expand(tiling_bounds, tiling_bottom_right_corner);

    // Compute the data bounds, in the SRS units.
    //
    // The most zoomed-in matrix gives the best value for the data bounds.
    // The border tiles of previous matrices can cover a greater area, as they
    // are always `tile_size` by `tile_size` and the covered area may not be
    // divisible by the tile size at every level.
    // However these areas cannot be considered as covered by the whole matrix
    // set, as there is no data at high zoom levels.
    // The low-zoom tiles going out of the data bounds is handled by the border
    // tile aspect property.
    double data_width =
        last_matrix.scale * hrz::ogc::PixelSize * last_matrix.size.x * last_matrix.tile_size.x;
    double data_height =
        last_matrix.scale * hrz::ogc::PixelSize * last_matrix.size.y * last_matrix.tile_size.y;
    lm::dvec2 data_bottom_right_corner = {
        last_matrix.top_left_corner.x + data_width,
        last_matrix.top_left_corner.y - data_height // WMTS tile coords increase southwards.
    };
    lm::dbbox2 data_bounds = lm::dbbox2::invalid();
    data_bounds = lm::expand(data_bounds, last_matrix.top_left_corner);
    data_bounds = lm::expand(data_bounds, data_bottom_right_corner);

    const auto& bbox_node = matrix_set_node.child("ows:BoundingBox");
    if (bbox_node)
    {
        auto bbox = parse_bounding_box(bbox_node);
        data_bounds = lm::intersection(data_bounds, bbox);
    }

    // @Todo The bounds computation is incorrect when the SRS coordinates increase southwards.
    // But we don't have that piece of information available. (The projection database could
    // be improved to contain this data.)

    TileMatrixSet data;
    data.google_maps_compatible = google_maps_compatible;
    data.crs_string = crs_string;
    data.crs = crs;
    data.projection_bounds = tiling_bounds;
    data.bounds = data_bounds;
    data.tile_size = first_matrix.tile_size.x;
    data.level_zero_tile_count = first_matrix.size;
    data.max_level_tile_count = last_matrix.size;
    data.identifier = matrix_set_node.child_value("ows:Identifier");

    for (const auto& matrix : tile_matrices)
    {
        data.matrix_identifiers.push_back(matrix.identifier);
    }

    return {data};
}

std::optional<TileMatrixSet> find_tile_matrix_set(
    const pugi::xml_node& layer_node,
    const pugi::xml_node& contents_node)
{
    // First search for a tile matrix set with the GoogleMapsCompatible profile.
    // Tile matrix sets with this profile can use blitting instead of reprojection
    // when composing tiles, so they are preferred for performance reasons.
    // Otherwise, select the first compatible tile matrix set.

    std::optional<TileMatrixSet> best_tile_matrix_set = std::nullopt;

    for (const auto& link_node : layer_node.children("TileMatrixSetLink"))
    {
        auto matrix_set_name = link_node.child_value("TileMatrixSet");

        for (const auto& matrix_set_node : contents_node.children("TileMatrixSet"))
        {
            if (std::strcmp(matrix_set_node.child_value("ows:Identifier"), matrix_set_name) == 0)
            {
                auto tile_matrix_set = parse_tile_matrix_set(matrix_set_node);

                if (tile_matrix_set.has_value())
                {
                    if (tile_matrix_set->google_maps_compatible)
                    {
                        return tile_matrix_set;
                    }

                    if (!best_tile_matrix_set.has_value())
                    {
                        best_tile_matrix_set = std::move(tile_matrix_set);
                    }
                }
            }
        }
    }

    return best_tile_matrix_set;
}

int8_t compute_level_offset(const lm::uvec2& level_zero_tile_count, uint32_t tile_size)
{
    return std::ceil(
        std::log2(tile_size * std::max(level_zero_tile_count.x, level_zero_tile_count.y)));
}
} // namespace

namespace hrz_jobs::parse_wmts_resource
{
hrz::JobResult run(
    const hrz::planet::WmtsResourceParams& params,
    hrz::planet::WmtsResourceResponse& response,
    const JobContext&)
{
    auto raw_xml = params.raw_xml.get_data();

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_buffer(raw_xml.data(), raw_xml.size());

    raw_xml.release();

    if (parse_result.status != pugi::status_ok)
    {
        HRZ_LOG_ERROR("Couldn't parse WMTS Capabilities XML: {}", parse_result.description());
        return hrz::JobResult::FAILURE;
    }

    const pugi::xml_node& root_node = doc.child("Capabilities");

    auto wmts_version = root_node.attribute("version").as_string("");
    if (std::strcmp(wmts_version, "") == 0)
    {
        HRZ_LOG_WARNING("No WMTS version specified, defaulting to 1.0.0.");
        wmts_version = "1.0.0";
    }

    auto get_tile_method = find_get_tile_method(root_node);

    if (!get_tile_method.has_value())
    {
        HRZ_LOG_ERROR("No supported GetTile method found");
        return hrz::JobResult::FAILURE;
    }

    const pugi::xml_node& contents_node = root_node.child("Contents");

    for (const auto& layer_node : contents_node.children("Layer"))
    {
        if (layer_node.child_value("ows:Identifier") == params.layer_identifier
            || params.layer_identifier.empty())
        {
            const auto layer_identifier = layer_node.child_value("ows:Identifier");

            // Style

            const char* style_name = nullptr;
            const char* first_style_name = nullptr;

            for (const auto& style_node : layer_node.children("Style"))
            {
                auto name = style_node.child_value("ows:Identifier");

                if (!params.style_identifier.empty())
                {
                    if (std::strcmp(name, params.style_identifier.c_str()) == 0)
                    {
                        style_name = name;
                        break;
                    }
                }
                else
                {
                    if (style_node.attribute("isDefault").as_bool())
                    {
                        style_name = name;
                        break;
                    }
                }

                if (first_style_name == nullptr)
                {
                    first_style_name = name;
                }
            }

            if (style_name == nullptr)
            {
                if (!params.style_identifier.empty())
                {
                    HRZ_LOG_ERROR(
                        "Style \"{}\" not found for layer \"{}\"", params.style_identifier,
                        layer_identifier);
                    return hrz::JobResult::FAILURE;
                }
                else
                {
                    style_name = first_style_name;

                    if (style_name == nullptr)
                    {
                        if (params.layer_identifier.empty())
                        {
                            continue;
                        }
                        else
                        {
                            HRZ_LOG_ERROR(
                                "No default style found for layer \"{}\"", layer_identifier);
                            return hrz::JobResult::FAILURE;
                        }
                    }
                }
            }

            // Format

            auto image_format = hrz::ogc::find_image_format(layer_node, params.image_format, false);

            if (!image_format.has_value())
            {
                if (params.layer_identifier.empty())
                {
                    continue;
                }
                else
                {
                    if (!params.image_format.empty())
                    {
                        HRZ_LOG_ERROR("Unavailable image format: {}", params.image_format);
                    }
                    else
                    {
                        HRZ_LOG_ERROR("No supported image format found");
                    }
                    return hrz::JobResult::FAILURE;
                }
            }

            // Resource URLs

            std::vector<const char*> url_templates;

            if (get_tile_method->method == hrz::planet::WmtsGetTileMethod::GET_RESTFUL)
            {
                for (const auto& resource_url_node : layer_node.children("ResourceURL"))
                {
                    if (std::strcmp(
                            resource_url_node.attribute("format").as_string(""),
                            image_format.value().data())
                        == 0)
                    {
                        url_templates.push_back(
                            resource_url_node.attribute("template").as_string());
                    }
                }

                if (url_templates.empty())
                {
                    if (params.layer_identifier.empty())
                    {
                        continue;
                    }
                    else
                    {
                        HRZ_LOG_ERROR("No template URLs for layer {}", layer_identifier);
                        return hrz::JobResult::FAILURE;
                    }
                }
            }

            // Bounding box

            std::optional<lm::dbbox2> layer_bbox = std::nullopt;

            const auto& bbox_node = layer_node.child("ows:BoundingBox");
            if (bbox_node)
            {
                layer_bbox = {parse_bounding_box(bbox_node)};
            }

            std::optional<lm::dbbox2> layer_wgs84_bbox = std::nullopt;

            const auto& wgs84_bbox_node = layer_node.child("ows:WGS84BoundingBox");
            if (wgs84_bbox_node)
            {
                layer_wgs84_bbox = {parse_bounding_box(wgs84_bbox_node)};
            }

            // Tile matrix set

            auto matrix_set_opt = find_tile_matrix_set(layer_node, contents_node);

            if (!matrix_set_opt.has_value())
            {
                if (params.layer_identifier.empty())
                {
                    continue;
                }
                else
                {
                    HRZ_LOG_ERROR(
                        "Could not find supported tile matrix set for layer \"{}\"",
                        layer_identifier);
                    return hrz::JobResult::FAILURE;
                }
            }

            const auto& matrix_set = matrix_set_opt.value();

            if (get_tile_method->method == hrz::planet::WmtsGetTileMethod::GET_RESTFUL)
            {
                for (const auto& layer_url_template : url_templates)
                {
                    static constexpr std::string_view kValidArgs[] = {
                        "Style", "TileMatrixSet", "TileMatrix", "TileRow", "TileCol"};
                    std::string url_template =
                        hrz::str::sanitize_named_fmt_arguments(layer_url_template, kValidArgs);
                    url_template = fmt::format(
                        fmt::runtime(layer_url_template), fmt::arg("Style", style_name),
                        fmt::arg("TileMatrixSet", matrix_set.identifier),
                        fmt::arg("TileMatrix", "{z}"), fmt::arg("TileRow", "{y}"),
                        fmt::arg("TileCol", "{x}"));

                    response.url_patterns.push_back(url_template);
                }
            }
            else if (get_tile_method->method == hrz::planet::WmtsGetTileMethod::GET_KVP)
            {
                auto protocol = hrz::url::protocol_s(get_tile_method->url);
                auto authority = hrz::url::authority_s(get_tile_method->url);
                auto path = hrz::url::path_s(get_tile_method->url);
                auto query = hrz::url::query_s(get_tile_method->url);

                bool add_and = !query.empty() && query.data()[query.size() - 1] != '&';

                std::string url_template = fmt::format(
                    "{Protocol}://{Authority}{Path}?{Query}{And}"
                    "service=WMTS&request=GetTile&version={Version}&"
                    "layer={Layer}&style={Style}&format={Format}&"
                    "TileMatrixSet={TileMatrixSet}&TileMatrix={TileMatrix}&"
                    "TileRow={TileRow}&TileCol={TileCol}",
                    fmt::arg(
                        "Protocol", fmt::basic_string_view<char>(protocol.data(), protocol.size())),
                    fmt::arg(
                        "Authority",
                        fmt::basic_string_view<char>(authority.data(), authority.size())),
                    fmt::arg("Path", fmt::basic_string_view<char>(path.data(), path.size())),
                    fmt::arg("Query", fmt::basic_string_view<char>(query.data(), query.size())),
                    fmt::arg("And", add_and ? "&" : ""), fmt::arg("Version", wmts_version),
                    fmt::arg("Layer", hrz::url::percent_encode(layer_identifier)),
                    fmt::arg("Style", hrz::url::percent_encode(style_name)),
                    fmt::arg("Format", hrz::url::percent_encode(image_format.value())),
                    fmt::arg("TileMatrixSet", hrz::url::percent_encode(matrix_set.identifier)),
                    fmt::arg("TileMatrix", "{z}"), fmt::arg("TileRow", "{y}"),
                    fmt::arg("TileCol", "{x}"));

                response.url_patterns.push_back(url_template);
            }
            else
            {
                if (params.layer_identifier.empty())
                {
                    continue;
                }
                else
                {
                    assert(false && "Unhandled case");
                    return hrz::JobResult::FAILURE;
                }
            }

            for (const auto& identifier : matrix_set.matrix_identifiers)
            {
                response.matrix_identifiers.push_back(identifier);
            }

            auto& geometry = response.geometry;

            if (is_global_web_mercator(matrix_set))
            {
                geometry.projection.set_descriptor_(hrz_proj::wmerc_proj_str);
                geometry.projection.set_descriptor_type(
                    hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
                geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::GLOBAL);

                auto tiling = geometry.tiling_scheme.mutable_global_tiling();
                tiling->set_tile_size(256);
                tiling->set_level_zero_tile_count_x(1);
                tiling->set_level_zero_tile_count_y(1);
                tiling->set_min_level(0);
                tiling->set_max_level(matrix_set.matrix_identifiers.size() - 1);
                tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
            }
            else
            {
                geometry.projection.set_descriptor_(matrix_set.crs_string);
                geometry.projection.set_descriptor_type(
                    hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR);
                geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::LOCAL);

                uint32_t max_level = matrix_set.matrix_identifiers.size() - 1;

                auto tiling = geometry.tiling_scheme.mutable_local_tiling();
                tiling->set_full_image_width(
                    matrix_set.level_zero_tile_count.x * ((uint64_t)1 << max_level)
                    * matrix_set.tile_size);
                tiling->set_full_image_height(
                    matrix_set.level_zero_tile_count.y * ((uint64_t)1 << max_level)
                    * matrix_set.tile_size);
                tiling->set_tile_size(matrix_set.tile_size);
                tiling->set_has_min_level(true);
                tiling->set_min_level(0);
                tiling->set_has_max_level(true);
                tiling->set_max_level(max_level);
                tiling->set_override_level_offset(true);
                tiling->set_level_offset(
                    compute_level_offset(matrix_set.level_zero_tile_count, matrix_set.tile_size));
                // @Todo Not quite the correct tile aspect behaviour.
                // When a tile is larger than the whole data bounds, the pixels
                // containing data are centered in the tile, instead of being
                // in the top-left corner.
                tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
                tiling->set_tiling_origin(hrz_proto::TilingOrigin::TOP_ORIGIN);

                geometry.projection_bounds = matrix_set.projection_bounds;
            }

            lm::dbbox2 bounds = matrix_set.bounds;

            if (layer_bbox.has_value())
            {
                bounds = lm::intersection(bounds, layer_bbox.value());
            }

            if (layer_wgs84_bbox.has_value())
            {
                bounds = transform_and_intersect_bbox(
                    bounds, layer_wgs84_bbox.value(), matrix_set.crs_string);
            }

            geometry.bounds = bounds;

            // A TileMatrixSetLink can define a TileMatrixSetLimits structure, that tells for each
            // TileMatrix the range in X and Y coordinates of the tiles that can be requested.
            // This is not compatible with how raster layers are used inside Horizon, as the limits
            // can define effective geographical bounds that are different for each zoom level.
            // Thankfully, WMTS servers tend instead to rely on BoundingBox structures, which are
            // valid for the whole TileMatrixSet. Due to this, I haven't encountered any public
            // server using TileMatrixSetLimits.
            //     -tpetillon, 2021-11-15

            return hrz::JobResult::SUCCESS;
        }
    }

    if (!params.layer_identifier.empty())
    {
        HRZ_LOG_ERROR("Could not find layer \"{}\'", params.layer_identifier);
    }
    else
    {
        HRZ_LOG_ERROR("Could not find any displayable layer");
    }

    return hrz::JobResult::FAILURE;
}
} // namespace hrz_jobs::parse_wmts_resource
