#include "hrz/core/jobs/parse_wms_resource.h"

#include "hrz/common/color.h"
#include "hrz/common/crs_database.h"
#include "hrz/common/crs_utils.h"
#include "hrz/common/geo.h"
#include "hrz/common/proj.h"
#include "hrz/core/jobs/context.h"
#include "hrz/core/jobs/job_result.h"
#include "hrz/core/jobs/ogc_utils.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/url_utils.h"

#include <fmt/format.h>
#include <lin_maths.h>
#include <proj_lite.h>
#include <pugixml/pugixml.hpp>

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

// See https://www.ogc.org/standards/wms

namespace
{
// Equivalent to OpenStreetMap level 20.
constexpr double DefaultMinScale = (hrz::MERCATOR_RANGE / (256 * (1 << 20))) / hrz::ogc::PixelSize;
constexpr double DefaultMinScale_OldWms = (hrz::MERCATOR_RANGE / (256 * (1 << 20))) * lm::SQRT2;

// Equivalent to OpenStreetMap level 0.
constexpr double DefaultMaxScale = (hrz::MERCATOR_RANGE / 256) / hrz::ogc::PixelSize;
constexpr double DefaultMaxScale_OldWms = (hrz::MERCATOR_RANGE / 256) * lm::SQRT2;

unsigned int get_layer_limit(const pugi::xml_node& root_node)
{
    unsigned int layer_limit = std::numeric_limits<unsigned int>::max();

    const auto& service_node = root_node.child("Service");
    if (!service_node) return layer_limit;

    const auto& layer_limit_node = service_node.child("LayerLimit");

    if (layer_limit_node)
    {
        layer_limit = layer_limit_node.text().as_uint(layer_limit);
    }

    return layer_limit;
}

unsigned int get_tile_size(const pugi::xml_node& root_node)
{
    unsigned int tile_size = 256;

    const auto& service_node = root_node.child("Service");
    if (!service_node) return tile_size;

    const auto& max_width_node = service_node.child("MaxWidth");

    if (max_width_node)
    {
        tile_size = std::min(tile_size, max_width_node.text().as_uint(256));
    }

    const auto& max_height_node = service_node.child("MaxHeight");

    if (max_height_node)
    {
        tile_size = std::min(tile_size, max_height_node.text().as_uint(256));
    }

    return tile_size;
}

const char* get_exception(const pugi::xml_node& capability_node)
{
    // Ordered by decreasing order of preference.
    // Proprietary MIME types are for WMS 1.1.0.
    const char* exceptions[] = {
        "INIMAGE", "application/vnd.ogc.se_inimage", "BLANK", "application/vnd.ogc.se_blank",
        "XML",     "application/vnd.ogc.se_xml",
    };

    const auto& exception_node = capability_node.child("Exception");
    if (!exception_node) return "INIMAGE";

    const char* best_exception = "INIMAGE";
    size_t best_exception_score = HRZ_ARRAY_COUNT(exceptions);

    for (const auto& format_node : exception_node)
    {
        auto exception = format_node.child_value();

        for (size_t i = 0; i < HRZ_ARRAY_COUNT(exceptions); ++i)
        {
            size_t score = i;

            if (std::strcmp(exception, exceptions[i]) == 0 && score < best_exception_score)
            {
                best_exception = exceptions[i];
                best_exception_score = score;

                if (score == 0) return best_exception;
            }
        }
    }

    return best_exception;
}

std::optional<std::string_view> find_get_map_url(const pugi::xml_node& get_map_node)
{
    auto dcp_type_node = get_map_node.child("DCPType");
    if (!dcp_type_node) return std::nullopt;

    auto http_node = dcp_type_node.child("HTTP");
    if (!http_node) return std::nullopt;

    // POST is also possible, but GET is required by the spec.
    auto get_node = http_node.child("Get");
    if (!get_node) return std::nullopt;

    auto online_resource_node = get_node.child("OnlineResource");
    if (!online_resource_node) return std::nullopt;

    const auto href = online_resource_node.attribute("xlink:href").as_string("");
    if (std::strcmp(href, "") == 0) return std::nullopt;

    return {href};
}

const char* get_layer_style(const pugi::xml_node& layer_node, std::string_view requested_style)
{
    if (requested_style.empty()) return "";

    for (const auto& style_node : layer_node)
    {
        const auto& name_node = style_node.child("Name");
        if (!name_node) continue;

        auto name = name_node.child_value();
        if (name == requested_style)
        {
            return name;
        }
    }

    const auto& parent_node = layer_node.parent();
    if (std::strcmp(parent_node.name(), "Layer") == 0)
    {
        return get_layer_style(parent_node, requested_style);
    }

    return "";
}

void get_layer_attribution(
    const pugi::xml_node& layer_node,
    std::vector<hrz_jobs::WmsResourceResponse::Attribution>& attributions)
{
    auto parent_node = layer_node.parent();
    if (std::strcmp(parent_node.name(), "Layer") == 0)
    {
        get_layer_attribution(parent_node, attributions);
    }

    auto attribution_node = layer_node.child("Attribution");
    attributions.push_back({
        attribution_node.child("Title").text().as_string(),
        attribution_node.child("OnlineResource").attribute("xlink:href").as_string(),
        attribution_node.child("LogoURL")
            .child("OnlineResource")
            .attribute("xlink:href")
            .as_string(),
    });
}

void fill_layer_srs_list(
    const pugi::xml_node& layer_node,
    hrz::flat_hash_map<std::string, size_t>& srs_list)
{
    // Each layer can define a different set of projections and
    // associated bounds. They can also inherit either of those,
    // add new projections to the inherited list, and override
    // bounds. We have to find a projection (and the associated
    // bounds that all the requested layers support).
    //
    // The capability node usually lists the common projection
    // systems, but not always, so we cannot count on it.
    //
    // We get the (ordered) list of projections supported by the
    // first requested layer. The earlier in the list, the higher
    // the priority. Then for each layer, as long as it supports a
    // projection in the list, it bounding box gets enlarged. If
    // it doesn't support it, the projection is removed from the
    // list of bounding boxes.
    //
    // We assume the projection systems are listed in decreasing
    // order of preference by the server. (The higher a projection
    // is in the list, the higher the chances the server doesn't
    // have to reproject the data before serving it, and the faster
    // the response can be sent.)
    // So we select the first projection in the list that the
    // engine supports.
    //
    // Some projection systems may be declared, without a corres-
    // ponding bounding box accompanying them. In that case the
    // specification says that it means the server can reproject
    // the data on the fly and accepts requests for the whole
    // domain. But we still need bounds (except for EPSG:3857 and
    // EPSG:4326) so we have to reject those cases.
    //
    // There is a technically-non-standard-but-agreed-up way to
    // serve pre-computed (or at least, cached) tiles through
    // WMS: WMS Tiling Client Recommendation, or WMS-C for short.
    // It has been developed by OSGeo at a time when there were
    // no standard ways to serve tiles.
    // See https://wiki.osgeo.org/wiki/WMS_Tiling_Client_Recommendation
    // It has nowadays been superseeded by newer protocols:
    //  * TMS, by OSGeo.
    //  * WMTS, by OGC. This is the official standard for tiled
    //    maps, from the people who made WMS.
    // There is no specific effort to comply to WMS-C here.

    size_t rank = srs_list.size();

    auto insert = [&](std::string srs)
    {
        if (srs_list.insert({srs, rank}).second)
        {
            // Increase rank if the SRS has been added,
            // i.e. if it wasn't already in the map.
            rank += 1;
        }
    };

    for (const auto& crs_node : layer_node.children("CRS"))
    {
        pl_Crs crs;
        bool convert_success = hrz::convert_crs(
            crs_node.child_value(), hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
        if (convert_success)
        {
            // The projection is supported.
            insert({crs_node.child_value()});
        }
    }

    // This is for old versions of WMS.
    for (const auto& srs_node : layer_node.children("SRS"))
    {
        const char* srs_list_str = srs_node.child_value();
        size_t srs_list_str_size = std::strlen(srs_list_str);

        size_t current_srs_start = 0;

        for (size_t i = 0; i <= srs_list_str_size; ++i)
        {
            char c = srs_list_str[i];

            if (c == ' ' || c == 0)
            {
                if (i - current_srs_start > 0)
                {
                    auto crs_str = std::string(
                        std::string_view(srs_list_str + current_srs_start, i - current_srs_start));
                    pl_Crs crs;
                    bool convert_success = hrz::convert_crs(
                        crs_str.c_str(), hrz_proto::SrsDescriptorType::SRID_DESCRIPTOR, &crs);
                    if (convert_success)
                    {
                        // The projection is supported.
                        insert({crs_str});
                    }
                }

                current_srs_start = i + 1;
            }
        }
    }

    const auto& parent_node = layer_node.parent();
    if (std::strcmp(parent_node.name(), "Layer") == 0)
    {
        fill_layer_srs_list(parent_node, srs_list);
    }
}

// Return projection systems associated with their rank.
hrz::flat_hash_map<std::string, size_t> get_layer_srs_list(const pugi::xml_node& layer_node)
{
    hrz::flat_hash_map<std::string, size_t> srs_list;
    fill_layer_srs_list(layer_node, srs_list);
    return srs_list;
}

bool srs_requires_bounds(std::string_view srs)
{
    // These projection systems are used for global tiling schemes. When
    // they are used, and no bounds are specified, the raster layer system
    // has fallback definitions for bounds that cover the whole planet.
    // Thus, they aren't required here.
    return !(srs == "EPSG:3857" || srs == "EPSG:4326" || srs == "CRS:84");
}

// This structure maintains a sorted map of projection systems, each of
// them associated with bounds.
// It is first initialised with a list of projection systems. (Taken from
// the SRS list of the first encountered WMS layer that has been requested
// for display.) This list is ordered, as we assume the higher a system
// is in the list, the faster the server can serve images in that system.
// The systems are inserted in `srs_indices`, associated to their index in
// the list.
// Then (still for that first layer), bounds are associated to the projec-
// tion systems. They are placed in the `bounds` array, at the index in the
// map. The WMS service may not specify bounds for every SRS in the list.
// This behaviour is valid according to the specification, it means the
// layer can be requested anywhere. But unless it's one of the projection
// systems listed in `srs_requires_bounds()`, we require bounds, in order
// to compute the geometry of the raster.
// Then for each subsequent layer we remove from the map the projection
// systems the layer doesn't support. (As they aren't eligible any more
// for the projection system used for the composition of all the requested
// layers.) They are removed from `srs_indices` (and so cannot be used to
// retrieve bounds). The array `bounds` is left untouched; it contains
// outdated information, but that's not an issue.
// For each of these layers, we expand the bounds in this structure with
// those specified for the layer.
// In the end, the projection systems that are still in the map are usable
// for all the requested layers. To select a projection system that will
// be used when requesting images, we just have to look for the system in
// `srs_indices` with the lowest index. The corresponding bounds are in
// `bounds`, at that index.
struct SrsBounds
{
    hrz::flat_hash_map<std::string, size_t> srs_indices;
    std::vector<lm::dbbox2> bounds;

    void init_srs_list(hrz::flat_hash_map<std::string, size_t>& srs_list)
    {
        assert(srs_indices.empty() && bounds.empty());

        srs_indices = std::move(srs_list);

        for (auto it : srs_indices)
        {
            assert(it.second < srs_indices.size());
            bounds.push_back(lm::dbbox2::invalid());
        }
    }

    void intersect_srs_list(const hrz::flat_hash_map<std::string, size_t>& srs_list)
    {
        for (auto it = srs_indices.begin(); it != srs_indices.end();)
        {
            if (srs_list.find(it->first) != srs_list.end())
            {
                ++it;
            }
            else
            {
                srs_indices.erase(it++);
            }
        }
    }

    void add_bounds(const std::string& srs, const lm::dbbox2& bounds)
    {
        auto it = srs_indices.find(srs);
        if (it == srs_indices.end())
        {
            this->bounds.push_back(bounds);
            srs_indices.insert({srs, this->bounds.size() - 1});
        }
    }

    void merge_with(const SrsBounds& other)
    {
        for (auto it1 = srs_indices.begin(); it1 != srs_indices.end();)
        {
            auto it2 = other.srs_indices.find(it1->first);
            if (it2 != other.srs_indices.end())
            {
                lm::dbbox2 bounds1 = bounds.at(it1->second);
                lm::dbbox2 bounds2 = other.bounds.at(it2->second);
                lm::dbbox2 new_bounds = lm::dbbox2::invalid();

                // If the bounds are undefined (i.e. invalid) for one of the
                // layers, we want the merged bounds to remain invalid, because
                // invalid bounds means "potentially covering the whole planet".
                if (lm::is_valid(bounds1) && lm::is_valid(bounds2))
                {
                    new_bounds = lm::merge(bounds1, bounds2);
                }

                bounds[it1->second] = new_bounds;
                ++it1;
            }
            else if (!srs_requires_bounds(it1->first))
            {
                ++it1;
            }
            else
            {
                srs_indices.erase(it1++);
            }
        }
    }
};

void get_layer_bounds(const pugi::xml_node& layer_node, SrsBounds& layer_bounds)
{
    for (const auto& bbox_node : layer_node.children("BoundingBox"))
    {
        auto crs_ptr = bbox_node.attribute("CRS").as_string(nullptr);
        if (crs_ptr == nullptr) crs_ptr = bbox_node.attribute("SRS").as_string(nullptr);

        std::string crs(crs_ptr);

        double x_min = bbox_node.attribute("minx").as_double(std::numeric_limits<double>::max());
        double y_min = bbox_node.attribute("miny").as_double(std::numeric_limits<double>::max());
        double x_max = bbox_node.attribute("maxx").as_double(std::numeric_limits<double>::lowest());
        double y_max = bbox_node.attribute("maxy").as_double(std::numeric_limits<double>::lowest());

        lm::dbbox2 bounds = lm::dbbox2::invalid();
        bounds = lm::expand(bounds, {x_min, y_min});
        bounds = lm::expand(bounds, {x_max, y_max});

        layer_bounds.add_bounds(crs, bounds);
    }

    const auto& parent_node = layer_node.parent();
    if (std::strcmp(parent_node.name(), "Layer") == 0)
    {
        get_layer_bounds(parent_node, layer_bounds);
    }
}

struct ScaleDenominators
{
    std::optional<double> min_scale;
    std::optional<double> max_scale;

    static ScaleDenominators merge(const ScaleDenominators& a, const ScaleDenominators& b)
    {
        std::optional<double> min = a.min_scale;
        std::optional<double> max = a.max_scale;

        if (!a.min_scale.has_value())
        {
            min = b.min_scale;
        }
        else if (b.min_scale.has_value())
        {
            min = {std::min(a.min_scale.value(), b.min_scale.value())};
        }

        if (!a.max_scale.has_value())
        {
            max = b.max_scale;
        }
        else if (b.max_scale.has_value())
        {
            max = {std::max(a.max_scale.value(), b.max_scale.value())};
        }

        return {min, max};
    }
};

ScaleDenominators get_layer_scale_denominators(const pugi::xml_node& layer_node)
{
    std::optional<double> min_scale = std::nullopt;
    std::optional<double> max_scale = std::nullopt;

    const auto& min_scale_node = layer_node.child("MinScaleDenominator");
    if (min_scale_node)
    {
        double value = min_scale_node.text().as_double(-1);
        if (value >= 0) min_scale = {value};
    }

    const auto& max_scale_node = layer_node.child("MaxScaleDenominator");
    if (max_scale_node)
    {
        double value = max_scale_node.text().as_double(-1);
        if (value >= 0) max_scale = {value};
    }

    if (!min_scale.has_value() && !max_scale.has_value())
    {
        // <ScaleHint> is how the scales are specified in older WMS versions.
        const auto& scale_hint_node = layer_node.child("ScaleHint");
        if (scale_hint_node)
        {
            {
                double value = scale_hint_node.attribute("min").as_double(-1);
                if (value >= 0) min_scale = {value};
            }
            {
                double value = scale_hint_node.attribute("max").as_double(-1);
                if (value >= 0) max_scale = {value};
            }
        }
    }

    if (!min_scale.has_value() || !max_scale.has_value())
    {
        const auto& parent_node = layer_node.parent();
        if (std::strcmp(parent_node.name(), "Layer") == 0)
        {
            auto parent_scales = get_layer_scale_denominators(parent_node);

            if (!min_scale.has_value()) min_scale = parent_scales.min_scale;
            if (!max_scale.has_value()) max_scale = parent_scales.max_scale;
        }
    }

    return {min_scale, max_scale};
}

bool is_layer_displayable(const pugi::xml_node& layer_node, unsigned int tile_size)
{
    bool no_subsets = layer_node.attribute("noSubsets").as_bool();
    auto fixed_width = layer_node.attribute("fixedWidth").as_uint(tile_size);
    auto fixed_height = layer_node.attribute("fixedHeight").as_uint(tile_size);

    // The layer cannot be displayed if subsets (i.e. tiles) cannot be created,
    // or if the tiles cannot have the desired size.
    return !no_subsets && fixed_width == tile_size && fixed_height == tile_size;
}

void get_layers(
    const pugi::xml_node& layer_node,
    const hrz_jobs::WmsResourceParams& params,
    unsigned int tile_size,
    size_t& found_layer_count,
    std::vector<bool>& found_layers,
    std::vector<const char*>& layer_names,
    std::vector<const char*>& layer_styles,
    std::vector<hrz_jobs::WmsResourceResponse::Attribution>& attributions,
    SrsBounds& srs_bounds,
    ScaleDenominators& scale_denominators)
{
    if (is_layer_displayable(layer_node, tile_size))
    {
        const auto& name_node = layer_node.child("Name");
        if (name_node)
        {
            auto name = name_node.child_value();

            for (size_t i = 0; i < layer_names.size(); ++i)
            {
                const auto& layer_params = params.layers.at(i);
                if (layer_params.layer_name == name)
                {
                    auto srs_list = get_layer_srs_list(layer_node);
                    if (found_layer_count == 0)
                    {
                        srs_bounds.init_srs_list(srs_list);
                    }
                    else
                    {
                        srs_bounds.intersect_srs_list(srs_list);
                    }

                    SrsBounds layer_bounds;
                    get_layer_bounds(layer_node, layer_bounds);
                    srs_bounds.merge_with(layer_bounds);

                    found_layer_count += 1;
                    found_layers[i] = true;
                    layer_names[i] = name;
                    layer_styles[i] = get_layer_style(layer_node, layer_params.style_name.c_str());
                    get_layer_attribution(layer_node, attributions);
                    scale_denominators = ScaleDenominators::merge(
                        scale_denominators, get_layer_scale_denominators(layer_node));
                }
            }
        }
    }

    for (const auto& child_layer_node : layer_node.children("Layer"))
    {
        get_layers(
            child_layer_node, params, tile_size, found_layer_count, found_layers, layer_names,
            layer_styles, attributions, srs_bounds, scale_denominators);
    }
}
} // namespace

namespace hrz_jobs::parse_wms_resource
{
hrz_jobs::JobResult run(
    const hrz_jobs::WmsResourceParams& params,
    hrz_jobs::WmsResourceResponse& response,
    const JobContext&)
{
    auto raw_xml = params.raw_xml.get_data();

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_buffer(raw_xml.data(), raw_xml.size());

    raw_xml.release();

    if (parse_result.status != pugi::status_ok)
    {
        HRZ_LOG_ERROR("Couldn't parse WMS Capabilities XML: {}", parse_result.description());
        return hrz_jobs::JobResult::FAILURE;
    }

    pugi::xml_node root_node = doc.child("WMS_Capabilities");
    if (!root_node) root_node = doc.child("WMT_MS_Capabilities");

    std::string_view wms_version = root_node.attribute("version").as_string("");
    bool old_wms_version = false;
    if (wms_version.empty() == 0)
    {
        HRZ_LOG_WARNING("No WMS version specified, defaulting to 1.3.0.");
        wms_version = "1.3.0";
    }
    else if (wms_version.starts_with("1.1") || wms_version.starts_with("1.0"))
    {
        old_wms_version = true;
    }

    const pugi::xml_node& capability_node = root_node.child("Capability");

    if (!capability_node)
    {
        HRZ_LOG_ERROR("Missing Capability node");
        return hrz_jobs::JobResult::FAILURE;
    }

    const pugi::xml_node& request_node = capability_node.child("Request");

    if (!request_node)
    {
        HRZ_LOG_ERROR("Missing Request node");
        return hrz_jobs::JobResult::FAILURE;
    }

    const auto& get_map_node = request_node.child("GetMap");

    if (!get_map_node)
    {
        HRZ_LOG_ERROR("Missing GetMap node");
        return hrz_jobs::JobResult::FAILURE;
    }

    unsigned int layer_limit = get_layer_limit(root_node);
    unsigned int requested_layer_count = std::min((unsigned int)params.layers.size(), layer_limit);

    unsigned int tile_size = get_tile_size(root_node);

    if (tile_size == 0)
    {
        HRZ_LOG_ERROR("Invalid max image size");
        return hrz_jobs::JobResult::FAILURE;
    }

    auto get_map_url = find_get_map_url(get_map_node);

    if (!get_map_url.has_value())
    {
        HRZ_LOG_ERROR("No GetMap URL found");
        return hrz_jobs::JobResult::FAILURE;
    }

    auto exception = get_exception(capability_node);

    const auto& root_layer_node = capability_node.child("Layer");

    bool opaque = root_layer_node.attribute("opaque").as_bool(false) || params.force_opaque;

    auto image_format = hrz::ogc::find_image_format(get_map_node, params.image_format, !opaque);

    if (!image_format.has_value())
    {
        if (!params.image_format.empty())
        {
            HRZ_LOG_ERROR("Unavailable image format: {}", params.image_format);
        }
        else
        {
            HRZ_LOG_ERROR("No supported image format found");
        }
        return hrz_jobs::JobResult::FAILURE;
    }

    SrsBounds srs_bounds;
    ScaleDenominators scale_denominators = {std::nullopt, std::nullopt};
    size_t found_layer_count = 0;
    std::vector<bool> found_layers(requested_layer_count, false);
    std::vector<const char*> layer_names(requested_layer_count, nullptr);
    std::vector<const char*> layer_styles(requested_layer_count, nullptr);

    get_layers(
        root_layer_node, params, tile_size, found_layer_count, found_layers, layer_names,
        layer_styles, response.attributions, srs_bounds, scale_denominators);

    {
        bool has_displayable_layer = false;

        for (size_t i = 0; i < requested_layer_count; ++i)
        {
            bool found = found_layers.at(i);

            if (!found)
            {
                HRZ_LOG_WARNING(
                    "Layer \"{}\" either does not exist, or is not displayable",
                    params.layers.at(i).layer_name);
            }

            has_displayable_layer |= found;
        }

        if (!has_displayable_layer)
        {
            HRZ_LOG_ERROR("No requested layers exist or are displayable");
            return hrz_jobs::JobResult::FAILURE;
        }
    }

    std::string srs;
    lm::dbbox2 bounds;

    // Select a projection system. See `SrsBounds`.
    {
        if (srs_bounds.srs_indices.empty())
        {
            HRZ_LOG_ERROR("No usable projection found.");
            return hrz_jobs::JobResult::FAILURE;
        }

        {
            size_t lowest_index = std::numeric_limits<size_t>::max();
            for (const auto& it : srs_bounds.srs_indices)
            {
                const auto& it_bounds = bounds = srs_bounds.bounds.at(it.second);
                bool valid_bounds = lm::is_valid(it_bounds) || !srs_requires_bounds(it.first);

                if (valid_bounds && it.second < lowest_index)
                {
                    srs = it.first;
                    lowest_index = it.second;
                }
            }

            bounds = srs_bounds.bounds.at(lowest_index);
        }
    }

    std::string proj_str = "";
    bool swap_axes = false;
    bool convert_degrees_to_meters = false;
    if (srs == "EPSG:3857")
    {
        proj_str = hrz_proj::wmerc_proj_str;
    }
    else if (srs == "EPSG:4326")
    {
        // "EPSG:4326" is the official EPSG:4326, where the latitude
        // is in the x axis, and the longitude in the y axis.
        proj_str = hrz_proj::lonlat_deg_proj_str;
    }
    else if (srs == "CRS:84")
    {
        // "CRS:84" is Horizon's (and many other tools) version of
        // EPSG:4326, where the longitude is in the x axis, and the
        // latitude in the y axis.
        proj_str = hrz_proj::lonlat_deg_proj_str;
    }
    else
    {
        proj_str = hrz::srid_descriptor_to_proj4(srs.data());
        convert_degrees_to_meters = proj_str.find("+proj=longlat") != std::string::npos;
    }

    // Latitude comes before longitude for some systems, including
    // EPSG:4326. The BBOX parameter in the request must take this
    // into account.
    // Older WMS versions do not swap axes.
    if (!old_wms_version)
    {
        auto srid = hrz::crs::parse_srid(srs);
        if (srid.has_value())
        {
            // Authority is EPSG.
            swap_axes = hrz::ogc::crs_has_flipped_axes(srid->authority, srid->code);
        }
    }

    {
        auto protocol = hrz::url::protocol_s(get_map_url.value());
        auto authority = hrz::url::authority_s(get_map_url.value());
        auto path = hrz::url::path_s(get_map_url.value());
        auto query = hrz::url::query_s(get_map_url.value());

        bool append_and = !query.empty() && query.data()[query.size() - 1] != '&';

        std::string layer_names_str = "";
        std::string layer_styles_str = "";
        for (size_t i = 0; i < requested_layer_count; ++i)
        {
            if (!found_layers.at(i)) continue;

            if (!layer_names_str.empty())
            {
                layer_names_str += ",";
                layer_styles_str += ",";
            }

            layer_names_str += hrz::url::percent_encode(layer_names.at(i));
            layer_styles_str += hrz::url::percent_encode(layer_styles.at(i));
        }

        std::string url_template = fmt::format(
            "{Protocol}://{Authority}{Path}?{Query}{And}"
            "SERVICE=WMS&VERSION={Version}&REQUEST=GetMap&"
            "LAYERS={Layers}&STYLES={Styles}&"
            "WIDTH={TileWidth}&HEIGHT={TileHeight}&FORMAT={Format}&"
            "{CrsKey}={CrsValue}&"
            "TRANSPARENT={Transparent}&BGCOLOR={BgColor}&"
            "EXCEPTIONS={Exception}&"
            "BBOX={Xmin},{Ymin},{Xmax},{Ymax}",
            fmt::arg("Protocol", protocol), fmt::arg("Authority", authority),
            fmt::arg("Path", path), fmt::arg("Query", query),
            fmt::arg("And", append_and ? "&" : ""), fmt::arg("Version", wms_version),
            fmt::arg("CrsKey", old_wms_version ? "SRS" : "CRS"),
            fmt::arg("CrsValue", hrz::url::percent_encode(srs)), fmt::arg("TileWidth", tile_size),
            fmt::arg("TileHeight", tile_size), fmt::arg("Layers", layer_names_str),
            fmt::arg("Styles", layer_styles_str),
            fmt::arg("Format", hrz::url::percent_encode(image_format.value())),
            fmt::arg("Transparent", opaque ? "FALSE" : "TRUE"),
            fmt::arg("BgColor", hrz::make_color_string(params.background_color, "0x", false, true)),
            fmt::arg("Exception", hrz::url::percent_encode(exception)),
            fmt::arg("Xmin", swap_axes ? "{south}" : "{west}"),
            fmt::arg("Ymin", swap_axes ? "{west}" : "{south}"),
            fmt::arg("Xmax", swap_axes ? "{north}" : "{east}"),
            fmt::arg("Ymax", swap_axes ? "{east}" : "{north}"));

        for (size_t i = 0; i < params.dimensions.size(); ++i)
        {
            const auto& dimension = params.dimensions.at(i);

            url_template += '&';
            url_template += hrz::url::percent_encode(dimension.name);
            url_template += '=';
            url_template += hrz::url::percent_encode(dimension.value);
        }

        response.url_template = url_template;
    }

    if (!scale_denominators.min_scale.has_value())
    {
        scale_denominators.min_scale = {old_wms_version ? DefaultMinScale_OldWms : DefaultMinScale};
    }
    if (!scale_denominators.max_scale.has_value())
    {
        scale_denominators.max_scale = {old_wms_version ? DefaultMaxScale_OldWms : DefaultMaxScale};
    }

    // WMS 1.3.0: Scales are unitless. They are size ratios between metres on the planet
    // and metres on the display. A physical pixel is assumed to be `hrz::ogc::PixelSize`
    // (0.00028) metres wide.
    // Older WMS: Scales are in metres per pixel. But they use the diagonal of the pixel
    // instead of the size length.
    double pixel_size = old_wms_version ? 1.0 / lm::SQRT2 : hrz::ogc::PixelSize;

    uint8_t min_level = 0;
    uint8_t max_level = 0;
    lm::dvec2 min_scale_full_image_size = {0, 0};

    auto round_eps = [](double level, bool round_up) -> uint8_t
    {
        if (level < 0) return 0;

        constexpr double eps = 1e-6; // As defined by the specification.
        double floored = std::floor(level);
        double diff = level - floored;
        if (diff <= eps) return floored;
        if (diff >= 1.0 - eps) return floored + 1;
        return round_up ? floored + 1 : floored;
    };

    if (srs == "EPSG:3857")
    {
        auto scale_to_lod = [&](double scale)
        { return std::log2(hrz::MERCATOR_RANGE / (scale * pixel_size * tile_size)); };

        max_level = round_eps(scale_to_lod(scale_denominators.min_scale.value()), false);
        min_level = round_eps(scale_to_lod(scale_denominators.max_scale.value()), true);
    }
    else if (srs == "EPSG:4326" || srs == "CRS:84")
    {
        auto scale_to_lod = [&](double scale)
        {
            // Use the circumference of the Earth at the Equator, per the specification.
            return std::log2(hrz::EARTH_CIRCUMFERENCE / (scale * pixel_size * tile_size));
        };

        auto adjust = [](uint8_t level) -> uint8_t
        {
            // Adjust level for tile count at level 0 ((2,1) in this tiling).
            if (level == 0) return level;
            return level - 1;
        };

        max_level = adjust(round_eps(scale_to_lod(scale_denominators.min_scale.value()), false));
        min_level = adjust(round_eps(scale_to_lod(scale_denominators.max_scale.value()), true));
    }
    else
    {
        lm::dbbox2 image_bounds = bounds;

        if (convert_degrees_to_meters)
        {
            // Use the circumference of the Earth at the Equator, per the specification.
            double factor = hrz::EARTH_CIRCUMFERENCE / 360;
            image_bounds.min *= factor;
            image_bounds.max *= factor;
        }

        lm::dvec2 bounds_size = lm::size(image_bounds);

        min_scale_full_image_size = {
            bounds_size.x / (scale_denominators.min_scale.value() * pixel_size),
            bounds_size.y / (scale_denominators.min_scale.value() * pixel_size)};
        lm::dvec2 max_scale_full_image_size = {
            bounds_size.x / (scale_denominators.max_scale.value() * pixel_size),
            bounds_size.y / (scale_denominators.max_scale.value() * pixel_size)};

        // Level with enough tiles to fit the most zoomed-in image the server can provide.
        if (std::max(min_scale_full_image_size.x, min_scale_full_image_size.y) > tile_size)
        {
            max_level = std::ceil(std::log2(
                std::max(min_scale_full_image_size.x, min_scale_full_image_size.y) / tile_size));
        }

        // Lowest level at which the image its self (not the full tiles) is larger than
        // (or has the same size as) the most zoomed-out image the server can provide.
        if (std::max(max_scale_full_image_size.x, max_scale_full_image_size.y) > tile_size)
        {
            min_level = std::ceil(
                max_level
                - log2(
                    std::max(min_scale_full_image_size.x, min_scale_full_image_size.y)
                    / std::max(max_scale_full_image_size.x, max_scale_full_image_size.y)));
        }

        if (params.override_max_level)
        {
            min_scale_full_image_size =
                min_scale_full_image_size * std::pow(2.0, (int32_t)params.max_level - max_level);
        }
    }

    if (params.override_min_level)
    {
        min_level = params.min_level;
    }
    if (params.override_max_level)
    {
        max_level = params.max_level;
    }

    {
        auto& geometry = response.geometry;

        if (srs == "EPSG:3857")
        {
            geometry.projection.set_descriptor_(proj_str);
            geometry.projection.set_descriptor_type(
                hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
            geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::GLOBAL);

            auto tiling = geometry.tiling_scheme.mutable_global_tiling();
            tiling->set_tile_size(256);
            tiling->set_level_zero_tile_count_x(1);
            tiling->set_level_zero_tile_count_y(1);
            tiling->set_min_level(min_level);
            tiling->set_max_level(max_level);
            tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
        }
        else if (srs == "EPSG:4326" || srs == "CRS:84")
        {
            geometry.projection.set_descriptor_(proj_str);
            geometry.projection.set_descriptor_type(
                hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
            geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::GLOBAL);

            auto tiling = geometry.tiling_scheme.mutable_global_tiling();
            tiling->set_tile_size(256);
            tiling->set_level_zero_tile_count_x(2);
            tiling->set_level_zero_tile_count_y(1);
            tiling->set_min_level(min_level);
            tiling->set_max_level(max_level);
            tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
        }
        else
        {
            geometry.projection.set_descriptor_(proj_str);
            geometry.projection.set_descriptor_type(
                hrz_proto::SrsDescriptorType::PROJ4_STRING_DESCRIPTOR);
            geometry.tiling_scheme.set_type(hrz_proto::TilingSchemeType::LOCAL);

            auto tiling = geometry.tiling_scheme.mutable_local_tiling();
            tiling->set_full_image_width(min_scale_full_image_size.x);
            tiling->set_full_image_height(min_scale_full_image_size.y);
            tiling->set_tile_size(tile_size);
            tiling->set_has_min_level(true);
            tiling->set_min_level(min_level);
            tiling->set_has_max_level(true);
            tiling->set_max_level(max_level);
            tiling->set_override_level_offset(false);
            tiling->set_border_tile_aspect(hrz_proto::BorderTileAspect::FULL_SIZED);
            tiling->set_tiling_origin(hrz_proto::TilingOrigin::TOP_ORIGIN);
        }

        if (swap_axes)
        {
            std::swap(bounds.min.x, bounds.min.y);
            std::swap(bounds.max.x, bounds.max.y);
        }

        assert(
            lm::is_valid(bounds)
            || geometry.tiling_scheme.type() == hrz_proto::TilingSchemeType::GLOBAL);
        if (lm::is_valid(bounds))
        {
            geometry.bounds = bounds;
        }

        if (geometry.tiling_scheme.type() == hrz_proto::TilingSchemeType::LOCAL)
        {
            geometry.projection_bounds = bounds;
        }
    }

    return hrz_jobs::JobResult::SUCCESS;
}
} // namespace hrz_jobs::parse_wms_resource
