#include "hrz_jobs_declarations.h"

#include <hrz_common_profiling.h>
#include <hrz_common_three_d_tiles.h>
#include <hrz_fnd_json_utils.h>
#include <hrz_fnd_log.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <limits>

static constexpr const char* SIRADEL_range_request = "SIRADEL_range_request";

namespace
{
using hrz::three_d_tiles::BoundingVolume;

bool check_json_document(const rapidjson::Document& document)
{
    if (document.HasParseError())
    {
        HRZ_LOG_ERROR(
            "Could not parse 3D Tiles tileset JSON: {}",
            rapidjson::GetParseError_En(document.GetParseError()));
        return false;
    }

    if (!document.IsObject() || !document.HasMember("asset"))
    {
        HRZ_LOG_ERROR("Invalid 3D Tiles tileset JSON");
        return false;
    }

    if (!document["asset"].HasMember("version") || !document["asset"]["version"].IsString())
    {
        HRZ_LOG_ERROR("No 3D Tiles tileset version field");
        return false;
    }

    const auto version = document["asset"]["version"].GetString();
    if (std::strcmp(version, "1.0") != 0)
    {
        HRZ_LOG_ERROR(
            "Unsupported 3D Tiles tileset version: Expected \"1.0\", got \"{}\"", version);
        return false;
    }

    return true;
}

lm::dmat4 parse_transform(const rapidjson::Value& transform_node)
{
    if (!transform_node.IsArray())
    {
        HRZ_LOG_ERROR("Invalid transform definition");
        return lm::dmat4::identity();
    }

    const auto& array = transform_node.GetArray();

    if (array.Size() != 16)
    {
        HRZ_LOG_ERROR("Invalid transform definition");
        return lm::dmat4::identity();
    }

    lm::dmat4 transform;

    for (unsigned int i = 0; i < 16; ++i)
    {
        const auto& entry = array[i];
        if (!entry.IsNumber())
        {
            HRZ_LOG_ERROR("Invalid transform definition");
            return lm::dmat4::identity();
        }

        transform.e[i] = entry.GetDouble();
    }

    return transform;
}

BoundingVolume parse_bounding_volume(const rapidjson::Value& volume_node)
{
    BoundingVolume volume;
    std::array<double, 12> values;

    auto read_values = [&](const char* node_name, unsigned int expected_value_count)
    {
        if (!volume_node[node_name].IsArray())
        {
            HRZ_LOG_ERROR("Invalid bounding {} definition", node_name);
            return false;
        }

        const auto& array = volume_node[node_name].GetArray();

        if (array.Size() > expected_value_count)
        {
            HRZ_LOG_ERROR("Invalid bounding {} definition", node_name);
            return false;
        }

        for (unsigned int i = 0; i < array.Size() && i < expected_value_count; ++i)
        {
            const auto& value = array[i];

            if (!value.IsNumber())
            {
                HRZ_LOG_ERROR("Invalid bounding {} definition", node_name);
                return false;
            }

            values[i] = value.GetDouble();
        }

        return true;
    };

    if (volume_node.HasMember("region"))
    {
        if (read_values("region", 6))
        {
            BoundingVolume::Region region;
            region.bounds.west = values[0];
            region.bounds.south = values[1];
            region.bounds.east = values[2];
            region.bounds.north = values[3];
            region.bounds.min_height = values[4];
            region.bounds.max_height = values[5];
            volume.volume = region;
        }
    }
    else if (volume_node.HasMember("box"))
    {
        if (read_values("box", 12))
        {
#define NORMALIZE_BOX_AXIS(x)                                \
    do                                                       \
    {                                                        \
        double length = lm::length(half_##x##_axis);         \
        if (length > std::numeric_limits<double>::epsilon()) \
        {                                                    \
            box.x##_axis = half_##x##_axis / length;         \
            box.x##_half_length = length;                    \
        }                                                    \
        else                                                 \
        {                                                    \
            box.x##_axis = {1.0, 0.0, 0.0};                  \
            box.x##_half_length = 0;                         \
        }                                                    \
    } while (0)

            BoundingVolume::Box box;
            box.center = {values[0], values[1], values[2]};
            lm::dvec3 half_u_axis = {values[3], values[4], values[5]};
            lm::dvec3 half_v_axis = {values[6], values[7], values[8]};
            lm::dvec3 half_w_axis = {values[9], values[10], values[11]};
            NORMALIZE_BOX_AXIS(u);
            NORMALIZE_BOX_AXIS(v);
            NORMALIZE_BOX_AXIS(w);
            volume.volume = box;

#undef NORMALIZE_BOX_AXIS
        }
    }
    else if (volume_node.HasMember("sphere"))
    {
        if (read_values("sphere", 4))
        {
            BoundingVolume::Sphere sphere;
            sphere.center = {values[0], values[1], values[2]};
            sphere.radius = values[3];
            volume.volume = sphere;
        }
    }
    else
    {
        HRZ_LOG_WARNING("No known bounding volume type found");
    }

    hrz::three_d_tiles::optimize(volume);

    return volume;
}

hrz::three_d_tiles::RefinementType parse_refinement_type(const rapidjson::Value& refinement_node)
{
    const auto str = refinement_node.GetString();

    if (std::strcmp(str, "ADD") == 0)
    {
        return hrz::three_d_tiles::RefinementType::ADD;
    }
    else if (std::strcmp(str, "REPLACE") == 0)
    {
        return hrz::three_d_tiles::RefinementType::REPLACE;
    }

    HRZ_LOG_WARNING("Invalid refinement type: {}", str);

    return hrz::three_d_tiles::RefinementType::REPLACE;
}

unsigned int parse_tile(
    const rapidjson::Value& tile_node,
    unsigned int parent_index,
    unsigned int depth,
    hrz::three_d_tiles::RefinementType parent_refinement_type,
    const lm::dmat4& parent_world_transform,
    const lm::dmat4& root_transform,
    hrz::three_d_tiles::ThreeDTilesTilesetDescriptor& response)
{
    unsigned int tile_index = response.tiles.size();
    response.tiles.push_back({});
    auto& tile = response.tiles.back();

    tile.parent_index = parent_index;
    tile.depth = depth;

    auto local_transform = tile_node.HasMember("transform")
        ? parse_transform(tile_node["transform"])
        : lm::dmat4::identity();
    auto world_transform = parent_world_transform * local_transform;
    tile.transform = world_transform;

    BoundingVolume local_bounding_volume;
    if (tile_node.HasMember("boundingVolume"))
    {
        local_bounding_volume = parse_bounding_volume(tile_node["boundingVolume"]);
    }
    else
    {
        HRZ_LOG_ERROR("No bounding volume for node");
    }
    auto world_bounding_volume =
        transform_bounding_volume(local_bounding_volume, world_transform, root_transform);
    tile.bounding_volume = world_bounding_volume;

    if (tile_node.HasMember("viewerRequestVolume"))
    {
        auto viewer_local_bounding_volume = parse_bounding_volume(tile_node["viewerRequestVolume"]);
        auto viewer_world_bounding_volume = transform_bounding_volume(
            viewer_local_bounding_volume, world_transform, root_transform);
        tile.viewer_bounding_volume = {viewer_world_bounding_volume};
    }

    tile.geometric_error = hrz::json::get_double_or(tile_node, "geometricError", 0);

    // "The transform property scales the geometricError by the largest scaling factor from the
    // matrix."
    {
        lm::dvec3 scales{
            lm::length(world_transform.x.xyz), lm::length(world_transform.y.xyz),
            lm::length(world_transform.z.xyz)};
        tile.geometric_error *= lm::maxelem(scales);
    }

    auto refinement_type = parent_refinement_type;
    if (tile_node.HasMember("refine"))
    {
        refinement_type = parse_refinement_type(tile_node["refine"]);
    }
    tile.refinement_type = refinement_type;

    if (tile_node.HasMember("content"))
    {
        const auto& content_node = tile_node["content"];

        if (content_node.HasMember("boundingVolume"))
        {
            auto content_local_bounding_volume =
                parse_bounding_volume(content_node["boundingVolume"]);
            auto content_world_bounding_volume = transform_bounding_volume(
                content_local_bounding_volume, world_transform, root_transform);
            tile.content_bounding_volume = {content_world_bounding_volume};
        }

        if (content_node.HasMember("uri"))
        {
            if (const auto& uri_node = content_node["uri"]; uri_node.IsString())
            {
                tile.uri = uri_node.GetString();
            }

            const auto& range_request = hrz::json::get_nested_member_or_null(
                content_node, {"extensions", SIRADEL_range_request});
            if (!range_request.IsNull())
            {
                tile.range.emplace(
                    hrz::json::get_uint64_or(range_request, "offset", 0),
                    hrz::json::get_uint64_or(range_request, "length", 0),
                    hrz::json::get_str(range_request, "mimeType"));
            }
        }
    }

    if (tile_node.HasMember("children"))
    {
        std::vector<unsigned int> child_indices;

        const auto& children_array = tile_node["children"].GetArray();

        for (const auto& child_node : children_array)
        {
            child_indices.push_back(parse_tile(
                child_node, tile_index, depth + 1, refinement_type, world_transform, root_transform,
                response));
        }

        // The existing reference may have been invalidated when
        // the child tiles have been added to the list, so we have
        // to make a new reference.
        auto& tile = response.tiles.at(tile_index);

        tile.child_count = child_indices.size();
        tile.first_child_link_index = response.child_links.size();

        for (unsigned int child_index : child_indices)
        {
            response.child_links.push_back(child_index);
        }
    }

    return tile_index;
}
} // namespace

namespace hrz_jobs::decode_three_d_tiles_tileset
{
hrz::JobResult run(
    const hrz::three_d_tiles::EncodedThreeDTilesTileset& params,
    hrz::three_d_tiles::ThreeDTilesTilesetDescriptor& response,
    const JobContext&)
{
    HRZ_SCOPED_SAMPLE("decode 3D tiles tileset");

    const lm::dmat4& root_transform = params.transform;
    const uint32_t root_depth = params.root_depth;

    rapidjson::Document document;
    {
        HRZ_SCOPED_SAMPLE("parse json");
        auto json_data = params.raw_json.get_data();
        document.Parse((const char*)json_data.data(), json_data.size());

        if (!check_json_document(document))
        {
            return hrz::JobResult::FAILURE;
        }
    }

    response.geometric_error = hrz::json::get_double_or(document, "geometricError", 0);

    if (!document.HasMember("root"))
    {
        HRZ_LOG_ERROR("No root node in tileset");
        return hrz::JobResult::FAILURE;
    }

    const auto& root = document["root"];
    response.root_tile_index = parse_tile(
        root, 0, root_depth, hrz::three_d_tiles::RefinementType::REPLACE, root_transform,
        root_transform, response);

    response.allow_gltf_content = false;

    const auto& required_ext_json = hrz::json::get_member_or_null(document, "extensionsRequired");
    if (required_ext_json.IsArray())
    {
        for (const auto& ext_json : required_ext_json.GetArray())
        {
            if (!ext_json.IsString()) continue;

            const auto& ext_name = ext_json.GetString();
            if (std::strcmp(ext_name, "3DTILES_content_gltf") == 0)
            {
                response.allow_gltf_content = true;
            }
            else if (std::strcmp(ext_name, "3DTILES_batch_table_hierarchy") == 0)
            {
                // Noop
            }
            else if (std::strcmp(ext_name, SIRADEL_range_request) == 0)
            {
                // Noop
            }
            else
            {
                HRZ_LOG_ERROR("Unsupported 3D Tiles extension {} required", ext_name);
                return hrz::JobResult::FAILURE;
            }
        }
    }

    return hrz::JobResult::SUCCESS;
}
} // namespace hrz_jobs::decode_three_d_tiles_tileset
