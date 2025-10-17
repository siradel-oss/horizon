#include "hrz_core_tilejson.h"

#include <hrz_common_profiling.h>
#include <hrz_fnd_json_utils.h>
#include <hrz_fnd_log.h>

#include <rapidjson/document.h>

namespace hrz::tilejson
{
namespace
{
enum class TileJsonVersion
{
    V1_0_0 = 100,
    V2_0_0 = 200,
    V2_0_1 = 201,
    V2_1_0 = 210,
    V2_2_0 = 220,
    V3_0_0 = 300,
    Latest = V3_0_0,
};

TileJsonVersion get_tilejson_version(const char* version_string)
{
    if (std::strcmp(version_string, "1.0.0") == 0)
    {
        return TileJsonVersion::V1_0_0;
    }
    else if (std::strcmp(version_string, "2.0.0") == 0)
    {
        return TileJsonVersion::V2_0_0;
    }
    else if (std::strcmp(version_string, "2.0.1") == 0)
    {
        return TileJsonVersion::V2_0_1;
    }
    else if (std::strcmp(version_string, "2.1.0") == 0)
    {
        return TileJsonVersion::V2_1_0;
    }
    else if (std::strcmp(version_string, "2.2.0") == 0)
    {
        return TileJsonVersion::V2_2_0;
    }
    else if (std::strcmp(version_string, "3.0.0") == 0)
    {
        return TileJsonVersion::V3_0_0;
    }
    else
    {
        HRZ_LOG_WARNING(
            "Unknown TileJSON version \"{}\". We'll try to parse anyway. Expect bugs.",
            version_string);
        return TileJsonVersion::Latest;
    }
}
} // namespace

std::optional<TileJsonInfo> parse_tilejson(
    std::span<const std::byte> raw_data,
    const BaseUrl& base_url)
{
    HRZ_SCOPED_SAMPLE("parse tilejson json");

    rapidjson::Document doc;
    doc.Parse((const char*)raw_data.data(), raw_data.size());

    if (doc.HasParseError())
    {
        HRZ_LOG_INFO("Couldn't parse layer descriptor");
        return std::nullopt;
    }

    auto tilejson_version = TileJsonVersion::Latest;
    auto tilejson_version_string = hrz::json::get_str_or(doc, "tilejson", "");
    if (std::strcmp(tilejson_version_string, "") == 0)
    {
        HRZ_LOG_WARNING(
            "TileJSON file is missing a 'tilejson' version field. We'll try to parse it anyway. "
            "Expect bugs.");
    }
    else
    {
        tilejson_version = get_tilejson_version(tilejson_version_string);
    }

    bool use_tms_tile_coords = false;
    auto scheme = hrz::json::get_str_or(doc, "scheme", "");
    if (std::strcmp(scheme, "tms") == 0)
    {
        use_tms_tile_coords = true;
    }

    std::vector<std::string> url_patterns;
    if (doc.HasMember("tiles") && doc["tiles"].IsArray())
    {
        const auto& patterns = doc["tiles"].GetArray();
        for (const auto& pattern : patterns)
        {
            if (pattern.IsString())
            {
                url_patterns.push_back(base_url.derive(pattern.GetString()));
            }
        }
    }

    if (url_patterns.empty())
    {
        HRZ_LOG_ERROR("No URL patterns found");
        return std::nullopt;
    }

    if (use_tms_tile_coords)
    {
        for (auto& url_pattern : url_patterns)
        {
            std::string::size_type n = 0;
            while ((n = url_pattern.find("{y}", n)) != std::string::npos)
            {
                url_pattern.replace(n, 3, "{-y}");
                n += 4;
            }
        }
    }

    uint32_t max_allowed_level =
        (uint32_t)tilejson_version >= (uint32_t)TileJsonVersion::V2_2_0 ? 30 : 22;

    uint32_t min_level = 0;
    auto min_zoom = hrz::json::get_int_or(doc, "minzoom", 0);
    if (min_zoom >= 0)
    {
        min_level = std::min((uint32_t)min_zoom, max_allowed_level);
    }

    uint32_t max_level = max_allowed_level;
    auto max_zoom = hrz::json::get_int_or(doc, "maxzoom", 0);
    if (max_zoom >= 0)
    {
        max_level = hrz::clamp((uint32_t)max_zoom, (uint32_t)0, max_allowed_level);
    }
    if (max_level < min_level)
    {
        HRZ_LOG_ERROR("No levels available");
        return std::nullopt;
    }

    hrz::GeoBounds bounds = {
        lm::radians(-180.0), lm::radians(180.0), -hrz::MERCATOR_MAX_LAT, hrz::MERCATOR_MAX_LAT};
    if (doc.HasMember("bounds") && doc["bounds"].IsArray())
    {
        const auto& bounds_array = doc["bounds"].GetArray();
        if (bounds_array.Size() == 4)
        {
            if (bounds_array[0].IsNumber())
            {
                bounds.west = lm::radians(bounds_array[0].GetDouble());
            }
            if (bounds_array[1].IsNumber())
            {
                bounds.south = lm::radians(bounds_array[1].GetDouble());
            }
            if (bounds_array[2].IsNumber())
            {
                bounds.east = lm::radians(bounds_array[2].GetDouble());
            }
            if (bounds_array[3].IsNumber())
            {
                bounds.north = lm::radians(bounds_array[3].GetDouble());
            }

            bounds = hrz::normalize(bounds);
        }
    }

    std::string attribution = hrz::json::get_str_or(doc, "attribution", "");

    return {{bounds, min_level, max_level, std::move(url_patterns), std::move(attribution)}};
}
} // namespace hrz::tilejson
