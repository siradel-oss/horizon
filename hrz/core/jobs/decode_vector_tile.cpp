#include "geobuf.pb.h"
#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_array.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proj.h"
#include "hrz/common/vector_data/geometry_utils.h"
#include "hrz/common/vector_data/packed_attribute_values_builder.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/core/jobs/vector_data_jobs_params.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/json_utils.h"
#include "hrz/fnd/log.h"
#include "vector_tile.pb.h"

#include <rapidjson/document.h>

#include <limits>
#include <optional>

// This job decodes the vector data stored in a tile file.
//
// All supported formats associate geometry with attribute values,
// as well as feature IDs. However none of them guarantee that
// feature IDs are unique. There can be multiple features, with
// different geometries and attribute values, that share the same
// ID.
//
// The decoded data returned by this job respects the order in
// which the features are defined in the input file. The decoded
// geometry, the attribute values, and the feature IDs have the
// data for one given feature at the same index in their respective
// arrays. The feature ID list can therefore have duplicated entries.
// (It isn't much different from a regular attribute.)

namespace hrz_jobs::decode_vector_tile
{
namespace
{

struct MutableVectorGeometry
{
    lm::dbbox2 bounds;
    hrz::BlobVector<hrz::vector_data::VectorTileGeometry::Feature> features;
    hrz::BlobVector<lm::dvec3> points;
    hrz::BlobVector<uint32_t> linestring_sizes;
};

struct MutableVectorTile
{
    hrz::TileCoords coords;
    MutableVectorGeometry geometry;
    std::vector<hrz::vector_data::PackedAttributeValuesBuilder> attributes;
};

static constexpr size_t InitialFeatureCapacity = 1024;
static constexpr size_t InitialPointCapacity = 4096;
static constexpr size_t InitialLinestringSizeCapacity = 1024;

enum Mvt_CommandType
{
    MVT_COMMAND_MOVE_TO = 1,
    MVT_COMMAND_LINE_TO = 2,
    MVT_COMMAND_CLOSE_PATH = 7
};

struct Mvt_Command
{
    explicit Mvt_Command(uint32_t command_integer) :
        type(command_integer & 0x7),
        count(command_integer >> 3),
        parameter_count(
            (type == MVT_COMMAND_MOVE_TO || type == MVT_COMMAND_LINE_TO) ? 2 * count : 0)
    {
    }

    uint8_t type;
    uint32_t count;
    uint32_t parameter_count;
};

inline int32_t zigzag_decode(uint32_t value)
{
    return (int32_t)((value >> 1) ^ (-(value & 1)));
}

// Compute area of simple polygons usings surveyor's formula
// ref: https://en.wikipedia.org/wiki/Shoelace_formula
double polygon_area(std::span<const lm::dvec3> linestring)
{
    const size_t n = linestring.size() - 1;
    double area = 0.0;

    for (size_t i = 0; i < n; i++)
    {
        // Vn.x * Vn+1.y - Vn+1.x * Vn.y
        area +=
            (linestring[i].x * (-linestring[i + 1].y) - linestring[i + 1].x * (-linestring[i].y));
    }
    area += (linestring[n].x * (-linestring[0].y) - linestring[0].x * (-linestring[n].y));
    return area * 0.5;
}

template<typename T>
void append_attribute_value(
    hrz::vector_data::PackedAttributeValuesBuilder& attribute,
    hrz_proto::AttributeTransform transform,
    T&& value,
    size_t count)
{
    if (transform == hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_NONE)
    {
        for (size_t i = 0; i < count; ++i)
        {
            attribute.push(std::forward<T>(value));
        }
    }
    else
    {
        auto ref_value = hrz::vector_data::attr_from<hrz::vector_data::RefAttributeValue>(
            std::forward<T>(value));
        auto new_value = hrz::vector_data::attr_transform<hrz::vector_data::OwnedAttributeValue>(
            transform, ref_value);
        for (size_t i = 0; i < count; ++i)
        {
            attribute.push_ref(hrz::vector_data::attr_as_ref(new_value));
        }
    }
}

// Returns whether the value type was handled. In any case a value is inserted.
bool append_json_attribute_value(
    hrz::vector_data::PackedAttributeValuesBuilder& attribute,
    hrz_proto::AttributeTransform transform,
    const rapidjson::Value& value,
    size_t count)
{
    bool ok = true;
    for (size_t i = 0; i < count; ++i)
    {
        ok = attribute.push_json(transform, value) && ok;
    }
    return ok;
}

void append_default_attribute_value(
    hrz::vector_data::PackedAttributeValuesBuilder& attribute,
    size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        attribute.push_null();
    }
}

inline lm::ivec2 mvt_decode_vertex(const vector_tile::Tile_Feature& feature, uint32_t index)
{
    return lm::ivec2(
        zigzag_decode(feature.geometry(index)), zigzag_decode(feature.geometry(index + 1)));
}

lm::dvec2 mvt_parameter_to_wmerc(lm::ivec2 cursor, uint32_t extent, const hrz::TileCoords& coords)
{
    const double world_quantization = (2 * hrz::MERCATOR_MAX_LAT_METERS) / (1 << coords.lod);
    const double tile_quantization = world_quantization / extent;
    // Flip the Y axis because MVT uses offsets from the top-left corner of the tile,
    // while EPSG:3857 uses the bottom-left corner as the origin.
    const int cursor_y = (int)extent - cursor.y;
    // Flip the Y coordinates as coords are given in XYZ form.
    const uint32_t coords_y = (1 << coords.lod) - 1 - coords.y;

    return lm::dvec2(
        cursor.x * tile_quantization + coords.x * world_quantization - hrz::MERCATOR_MAX_LAT_METERS,

        cursor_y * tile_quantization + coords_y * world_quantization
            - hrz::MERCATOR_MAX_LAT_METERS);
}

void mvt_parse_unknown(
    MutableVectorTile& tile,
    const vector_tile::Tile_Feature& feature,
    uint32_t extent)
{
    HRZ_SCOPED_SAMPLE_A("mvt vector tiles parse unknown");

    hrz::vector_data::VectorTileGeometry::Feature f;
    f.type = hrz_proto::VectorGeometryType::POLYGON_GEOMETRY;
    f.first_point = 0;
    f.point_count = 0;
    f.first_linestring_size = 0;
    f.linestring_count = 0;
    f.anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
    f.anchor_angle = 0;
    tile.geometry.features.push_back(f);
}

void mvt_parse_points(
    MutableVectorTile& tile,
    const vector_tile::Tile_Feature& feature,
    uint32_t extent)
{
    HRZ_SCOPED_SAMPLE_A("mvt vector tiles parse points");

    if (feature.geometry_size() == 0
        || !(feature.geometry(0) & Mvt_CommandType::MVT_COMMAND_MOVE_TO))
    {
        HRZ_LOG_WARNING(
            "Couldn't parse vector tile ({}/{}/{})", tile.coords.lod, tile.coords.x, tile.coords.y);
        return;
    }

    const Mvt_Command cmd(feature.geometry(0));
    lm::ivec2 cursor(0, 0);
    for (unsigned int i = 1; i < cmd.parameter_count + 1; i += 2)
    {
        cursor = cursor + mvt_decode_vertex(feature, i);
        const lm::dvec2 wmerc = mvt_parameter_to_wmerc(cursor, extent, tile.coords);

        hrz::vector_data::VectorTileGeometry::Feature f;
        f.type = hrz_proto::VectorGeometryType::POINT_GEOMETRY;
        f.first_point = tile.geometry.points.size().value_or(0);
        f.point_count = 1;
        f.first_linestring_size = 0;
        f.linestring_count = 0;
        f.anchor = {wmerc, 0.0};
        f.anchor_angle = 0;
        tile.geometry.features.push_back(f);

        tile.geometry.points.push_back({wmerc.x, wmerc.y, 0.0});
    }
}

void compute_polyline_anchor(
    MutableVectorTile& tile,
    hrz::vector_data::VectorTileGeometry::Feature& f)
{
    auto points_data = tile.geometry.points.data();
    if (!points_data.has_value()) return;

    auto linestring_sizes_data = tile.geometry.linestring_sizes.data();
    if (!linestring_sizes_data.has_value()) return;

    auto points = points_data->subspan(f.first_point, f.point_count);
    auto linestring_sizes =
        linestring_sizes_data->subspan(f.first_linestring_size, f.linestring_count);

    lm::dvec3 anchor;
    float anchor_angle{};
    hrz::vector_data::compute_linestring_middle_and_angle(
        points, linestring_sizes, &anchor, &anchor_angle);

    f.anchor = anchor;
    f.anchor_angle = anchor_angle;
}

void mvt_parse_linestrings(
    MutableVectorTile& tile,
    const vector_tile::Tile_Feature& feature,
    uint32_t extent)
{
    HRZ_SCOPED_SAMPLE_A("mvt vector tiles parse linestrings");

    hrz::vector_data::VectorTileGeometry::Feature f = {};
    f.type = hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY;
    f.first_point = tile.geometry.points.size().value_or(0);
    f.point_count = 0;
    f.first_linestring_size = tile.geometry.linestring_sizes.size().value_or(0);
    f.linestring_count = 0;
    f.anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
    f.anchor_angle = 0;

    lm::ivec2 cursor(0, 0);

    uint32_t current_linestring_point_count = 0;

    auto push_linestring = [&]()
    {
        tile.geometry.linestring_sizes.push_back(current_linestring_point_count);
        f.point_count += current_linestring_point_count;
        f.linestring_count += 1;
    };

    for (int i = 0; i < feature.geometry_size();)
    {
        const Mvt_Command cmd(feature.geometry(i));
        i += 1;

        if (cmd.type == Mvt_CommandType::MVT_COMMAND_MOVE_TO)
        {
            // Start a new linestring
            cursor = cursor + mvt_decode_vertex(feature, i);
            i += 2;

            if (current_linestring_point_count > 0)
            {
                push_linestring();
            }

            current_linestring_point_count = 1;

            const lm::dvec2 wmerc = mvt_parameter_to_wmerc(cursor, extent, tile.coords);
            tile.geometry.points.push_back({wmerc.x, wmerc.y, 0.0});
        }
        else if (cmd.type == Mvt_CommandType::MVT_COMMAND_LINE_TO)
        {
            if (current_linestring_point_count == 0)
            {
                HRZ_LOG_WARNING("Invalid polyline geometry");
                return;
            }

            const int line_end = i + cmd.parameter_count;
            for (; i < line_end; i += 2)
            {
                cursor = cursor + mvt_decode_vertex(feature, i);

                current_linestring_point_count += 1;

                const lm::dvec2 wmerc = mvt_parameter_to_wmerc(cursor, extent, tile.coords);
                tile.geometry.points.push_back({wmerc.x, wmerc.y, 0.0});
            }
        }
    }

    push_linestring();

    compute_polyline_anchor(tile, f);
    tile.geometry.features.push_back(f);
}

void mvt_parse_polygons(
    MutableVectorTile& tile,
    const vector_tile::Tile_Feature& feature,
    uint32_t extent)
{
    HRZ_SCOPED_SAMPLE_A("mvt vector tiles parse polygons");

    lm::ivec2 cursor(0, 0);
    std::optional<hrz::vector_data::VectorTileGeometry::Feature> f = std::nullopt;
    bool geometry_is_valid = false;

    uint32_t current_ring_first_point = 0;
    uint32_t current_ring_point_count = 0;

    auto push_feature = [&]()
    {
        if (geometry_is_valid)
        {
            assert(f.has_value());
            tile.geometry.features.push_back(f.value());
        }

        f = {hrz::vector_data::VectorTileGeometry::Feature()};
        f->type = hrz_proto::VectorGeometryType::POLYGON_GEOMETRY;
        f->first_point = current_ring_first_point;
        f->point_count = 0;
        f->first_linestring_size = tile.geometry.linestring_sizes.size().value_or(0);
        f->linestring_count = 0;
        f->anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
        f->anchor_angle = 0;

        geometry_is_valid = false;
    };

    for (int i = 0; i < feature.geometry_size();)
    {
        Mvt_Command cmd(feature.geometry(i));
        i += 1;

        if (cmd.type == Mvt_CommandType::MVT_COMMAND_MOVE_TO)
        {
            // Start a new linestring
            cursor = cursor + mvt_decode_vertex(feature, i);
            i += 2;

            current_ring_first_point = tile.geometry.points.size().value_or(0);
            current_ring_point_count = 1;

            const lm::dvec2 wmerc = mvt_parameter_to_wmerc(cursor, extent, tile.coords);
            tile.geometry.points.push_back({wmerc, 0.0});
        }
        else if (cmd.type == Mvt_CommandType::MVT_COMMAND_LINE_TO)
        {
            const int line_end = i + cmd.parameter_count;
            for (; i < line_end; i += 2)
            {
                current_ring_point_count += 1;
                cursor = cursor + mvt_decode_vertex(feature, i);

                const lm::dvec2 wmerc = mvt_parameter_to_wmerc(cursor, extent, tile.coords);
                tile.geometry.points.push_back({wmerc, 0.0});
            }
        }
        else if (cmd.type == Mvt_CommandType::MVT_COMMAND_CLOSE_PATH)
        {
            auto points_data_opt = tile.geometry.points.data();
            if (!points_data_opt.has_value())
            {
                return;
            }

            auto linestring =
                points_data_opt->subspan(current_ring_first_point, current_ring_point_count);

            // Exterior linestring => new polygon
            if (polygon_area(linestring) >= 0)
            {
                push_feature();

                lm::dvec3 centroid = hrz::vector_data::compute_ring_average(linestring);
                f->anchor = centroid;
                f->anchor_angle = 0;
            }

            if (!f.has_value())
            {
                HRZ_LOG_WARNING("Invalid polygon geometry");
                return;
            }

            f->point_count = f->point_count + current_ring_point_count;
            tile.geometry.linestring_sizes.push_back(current_ring_point_count);
            f->linestring_count += 1;

            geometry_is_valid = true;
        }
    }

    push_feature();
}

// Get a numeric value from a MVT value, taking type priority into account.
// The first parameter type is taken first if present, then the second one,
// and so on.
// (The last line is there to force adding a semicolon at the call-site.)
#define GET_MVT_NUMERIC_VALUE(in, type0, type1, type2, type3, type4, out) \
    if (in.has_##type0##_value())                                         \
    {                                                                     \
        out = in.type0##_value();                                         \
    }                                                                     \
    else if (in.has_##type1##_value())                                    \
    {                                                                     \
        out = in.type1##_value();                                         \
    }                                                                     \
    else if (in.has_##type2##_value())                                    \
    {                                                                     \
        out = in.type2##_value();                                         \
    }                                                                     \
    else if (in.has_##type3##_value())                                    \
    {                                                                     \
        out = in.type3##_value();                                         \
    }                                                                     \
    else if (in.has_##type4##_value())                                    \
    {                                                                     \
        out = in.type4##_value();                                         \
    }                                                                     \
                                                                          \
    static_assert(true, "")

bool decode_mvt(
    const hrz_jobs::EncodedVectorTile& encoded_tile,
    MutableVectorTile& decoded_tile,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    HRZ_SCOPED_SAMPLE("vector tiles job decode mvt");

    if (!std::holds_alternative<hrz_jobs::ParsedMvt>(encoded_tile.data.data))
    {
        HRZ_LOG_ERROR("Expected parsed MVT as input");
        return false;
    }

    auto vt = std::get<hrz_jobs::ParsedMvt>(encoded_tile.data.data).tile;

    decoded_tile.coords = encoded_tile.coords;

    auto should_decode_layer =
        [&](const vector_tile::Tile_Layer& mvt_layer, bool show_version_error)
    {
        if (encoded_tile.layer_name != "" && mvt_layer.name() != encoded_tile.layer_name)
        {
            return false;
        }

        if (mvt_layer.version() != 2)
        {
            if (show_version_error)
            {
                HRZ_LOG_ERROR(
                    "Unsupported MVT version for layer \"{}\": {}", mvt_layer.name(),
                    mvt_layer.version());
            }

            return false;
        }

        return true;
    };

    uint32_t feature_count = 0;
    for (auto& mvt_layer : vt->layers())
    {
        if (should_decode_layer(mvt_layer, true))
        {
            feature_count += mvt_layer.features_size();
        }
    }

    hrz::flat_hash_map<uint32_t, size_t> attribute_ids_to_indices;
    {
        for (size_t i = 0; i < encoded_tile.attributes.size(); ++i)
        {
            auto& encoded_attribute = encoded_tile.attributes.at(i);
            decoded_tile.attributes.emplace_back(feature_count, blob_allocator, resource_owner);
            attribute_ids_to_indices.insert({encoded_attribute.id, i});
        }
    }

    std::vector<bool> attributes_with_errors(decoded_tile.attributes.size(), false);

    const uint32_t NOT_NEEDED = 0xffffffff;

    // Index in MVT keys => attribute ID
    // This is reset at every layer.
    std::vector<uint32_t> attribute_mappings;

    // Index in decoded tile to whether values were decoded for the feature.
    // This is reset at every feature.
    std::vector<bool> attributes_with_values(encoded_tile.attributes.size(), false);

    HRZ_BEGIN_SAMPLE("tile parsing");

    for (auto& mvt_layer : vt->layers())
    {
        if (!should_decode_layer(mvt_layer, false))
        {
            continue;
        }

        attribute_mappings.clear();
        attribute_mappings.reserve(mvt_layer.keys_size());

        for (uint32_t key_index = 0; key_index < (uint32_t)mvt_layer.keys_size(); ++key_index)
        {
            const std::string& mvt_attribute = mvt_layer.keys(key_index);

            std::optional<uint32_t> attribute_id = std::nullopt;

            for (size_t i = 0; i < encoded_tile.attributes.size(); ++i)
            {
                const auto& encoded_attribute = encoded_tile.attributes.at(i);
                if (i != encoded_tile.source_feature_id_attribute
                    && encoded_attribute.name == mvt_attribute)
                {
                    attribute_id = {encoded_attribute.id};
                    break;
                }
            }

            attribute_mappings.push_back(attribute_id.value_or(NOT_NEEDED));
        }

        for (const auto& mvt_feature : mvt_layer.features())
        {
            size_t new_features_count = 1;

            if (encoded_tile.decode_geometry)
            {
                auto feature_count_before_opt = decoded_tile.geometry.features.size();
                if (!feature_count_before_opt.has_value()) return false;
                size_t feature_count_before = feature_count_before_opt.value();

                switch (mvt_feature.type())
                {
                    case vector_tile::Tile_GeomType_UNKNOWN:
                        mvt_parse_unknown(decoded_tile, mvt_feature, mvt_layer.extent());
                        break;
                    case vector_tile::Tile_GeomType_POINT:
                        mvt_parse_points(decoded_tile, mvt_feature, mvt_layer.extent());
                        break;
                    case vector_tile::Tile_GeomType_LINESTRING:
                        mvt_parse_linestrings(decoded_tile, mvt_feature, mvt_layer.extent());
                        break;
                    case vector_tile::Tile_GeomType_POLYGON:
                        mvt_parse_polygons(decoded_tile, mvt_feature, mvt_layer.extent());
                        break;
                    default:
                        HRZ_LOG_WARNING(
                            "Unknown vector tile geometry type: {}", (int)mvt_feature.type());
                        break;
                }

                auto feature_count_after_opt = decoded_tile.geometry.features.size();
                if (!feature_count_after_opt.has_value()) return false;
                new_features_count = feature_count_after_opt.value() - feature_count_before;
            }

            for (size_t i = 0; i < attributes_with_values.size(); ++i)
            {
                attributes_with_values[i] = false;
            }

            for (int32_t i = 0; i < mvt_feature.tags_size(); i += 2)
            {
                uint32_t mvt_attribute_index = mvt_feature.tags(i);
                uint32_t value_index = mvt_feature.tags(i + 1);

                const auto& value = mvt_layer.values().Get(value_index);

                if (mvt_attribute_index < attribute_mappings.size()
                    && attribute_mappings[mvt_attribute_index] != NOT_NEEDED)
                {
                    uint32_t attribute_id = attribute_mappings[mvt_attribute_index];
                    size_t decoded_attribute_index = attribute_ids_to_indices.at(attribute_id);
                    auto& decoded_attribute = decoded_tile.attributes.at(decoded_attribute_index);
                    auto transform = encoded_tile.attributes.at(decoded_attribute_index).transform;

                    if (value.has_bool_value())
                    {
                        append_attribute_value(
                            decoded_attribute, transform, value.bool_value(), new_features_count);
                        attributes_with_values[decoded_attribute_index] = true;
                    }
                    else if (value.has_float_value())
                    {
                        append_attribute_value(
                            decoded_attribute, transform, value.float_value(), new_features_count);
                        attributes_with_values[decoded_attribute_index] = true;
                    }
                    else if (value.has_double_value())
                    {
                        append_attribute_value(
                            decoded_attribute, transform, value.double_value(), new_features_count);
                        attributes_with_values[decoded_attribute_index] = true;
                    }
                    else if (value.has_int_value())
                    {
                        append_attribute_value(
                            decoded_attribute, transform, value.int_value(), new_features_count);
                        attributes_with_values[decoded_attribute_index] = true;
                    }
                    else if (value.has_uint_value())
                    {
                        append_attribute_value(
                            decoded_attribute, transform, value.uint_value(), new_features_count);
                        attributes_with_values[decoded_attribute_index] = true;
                    }
                    else if (value.has_sint_value())
                    {
                        append_attribute_value(
                            decoded_attribute, transform, value.sint_value(), new_features_count);
                        attributes_with_values[decoded_attribute_index] = true;
                    }
                    else if (value.has_string_value())
                    {
                        append_attribute_value(
                            decoded_attribute, transform, value.string_value(), new_features_count);
                        attributes_with_values[decoded_attribute_index] = true;
                    }
                    else
                    {
                        // The attribute key was present, but a value of the expected
                        // type could not be read.
                        attributes_with_errors[decoded_attribute_index] = true;
                    }
                }
            }

            for (size_t i = 0; i < attributes_with_values.size(); ++i)
            {
                if (i != encoded_tile.source_feature_id_attribute && !attributes_with_values[i])
                {
                    auto& decoded_attribute = decoded_tile.attributes.at(i);
                    append_default_attribute_value(decoded_attribute, new_features_count);
                }
            }

            if (encoded_tile.source_feature_id_attribute.has_value())
            {
                auto index = encoded_tile.source_feature_id_attribute.value();
                auto& feature_id_attribute = decoded_tile.attributes.at(index);
                auto transform = encoded_tile.attributes.at(index).transform;
                append_attribute_value(
                    feature_id_attribute, transform, mvt_feature.id(), new_features_count);
            }
        }

        if (encoded_tile.layer_name != "")
        {
            break; // No need to continue, as the layer has been found.
        }
    }

    // Compute the bounding box of the points
    decoded_tile.geometry.bounds = lm::dbbox2::invalid();
    auto points_data_opt = decoded_tile.geometry.points.data();
    if (points_data_opt.has_value() && !points_data_opt->empty())
    {
        for (const auto& point : points_data_opt.value())
        {
            decoded_tile.geometry.bounds = lm::expand(decoded_tile.geometry.bounds, point.xy);
        }
    }

    for (size_t i = 0; i < decoded_tile.attributes.size(); ++i)
    {
        if (attributes_with_errors[i])
        {
            HRZ_LOG_WARNING(
                "Error while reading values for attribute {} in tile {}-{}-{}",
                encoded_tile.attributes[i].id, encoded_tile.coords.lod, encoded_tile.coords.x,
                encoded_tile.coords.y);
        }
    }

    HRZ_END_SAMPLE();

    return true;
}

void reserve_attribute_space(
    std::vector<hrz::vector_data::PackedAttributeValuesBuilder>& decoded_attributes,
    size_t feature_count)
{
    for (auto& decoded_attribute : decoded_attributes)
    {
        decoded_attribute.reserve(feature_count);
    }
}

lm::dvec3 decode_geojson_point(const rapidjson::Value& coords)
{
    lm::dvec3 vector(0.0, 0.0, 0.0);
    int component = 0;

    if (coords.IsArray())
    {
        for (const auto& c : coords.GetArray())
        {
            if (c.IsNumber())
            {
                vector.m[component] = c.GetDouble();
            }

            component += 1;
            if (component >= 3) break;
        }
    }

    return vector;
}

void decode_geojson_point_geometry(MutableVectorTile& tile, const rapidjson::Value& coords)
{
    hrz::vector_data::VectorTileGeometry::Feature f;
    f.first_point = tile.geometry.points.size().value_or(0);
    f.point_count = 1;
    f.first_linestring_size = 0;
    f.linestring_count = 0;
    f.type = hrz_proto::VectorGeometryType::POINT_GEOMETRY;
    tile.geometry.features.push_back(f);

    tile.geometry.points.push_back(decode_geojson_point(coords));
}

void decode_geojson_multi_point_geometry(MutableVectorTile& tile, const rapidjson::Value& coords)
{
    if (coords.IsArray())
    {
        for (const auto& pt : coords.GetArray())
        {
            decode_geojson_point_geometry(tile, pt);
        }
    }
}

void decode_geojson_line_string_geometry(MutableVectorTile& tile, const rapidjson::Value& coords)
{
    if (coords.IsArray())
    {
        hrz::vector_data::VectorTileGeometry::Feature f;
        f.type = hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY;
        f.first_point = tile.geometry.points.size().value_or(0);
        f.point_count = 0;
        f.first_linestring_size = tile.geometry.linestring_sizes.size().value_or(0);
        f.linestring_count = 1;

        for (const auto& pt : coords.GetArray())
        {
            tile.geometry.points.push_back(decode_geojson_point(pt));
            f.point_count += 1;
        }

        tile.geometry.linestring_sizes.push_back(f.point_count);
        tile.geometry.features.push_back(f);
    }
}

void decode_geojson_multi_line_string_geometry(
    MutableVectorTile& tile,
    const rapidjson::Value& coords_array)
{
    if (coords_array.IsArray())
    {
        hrz::vector_data::VectorTileGeometry::Feature f;
        f.type = hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY;
        f.first_point = tile.geometry.points.size().value_or(0);
        f.point_count = 0;
        f.first_linestring_size = tile.geometry.linestring_sizes.size().value_or(0);
        f.linestring_count = 0;

        uint32_t linestring_size = 0;
        for (const auto& coords : coords_array.GetArray())
        {
            if (coords.IsArray())
            {
                linestring_size = 0;

                for (const auto& point : coords.GetArray())
                {
                    tile.geometry.points.push_back(decode_geojson_point(point));
                    linestring_size += 1;
                }

                f.point_count += linestring_size;
                f.linestring_count += 1;

                tile.geometry.linestring_sizes.push_back(linestring_size);
            }
        }

        tile.geometry.features.push_back(f);
    }
}

void decode_geojson_polygon_geometry(MutableVectorTile& tile, const rapidjson::Value& polygon)
{
    HRZ_SCOPED_SAMPLE_A("decode geojson polygon");

    hrz::vector_data::VectorTileGeometry::Feature f;
    f.type = hrz_proto::VectorGeometryType::POLYGON_GEOMETRY;
    f.first_point = tile.geometry.points.size().value_or(0);
    f.point_count = 0;
    f.first_linestring_size = tile.geometry.linestring_sizes.size().value_or(0);
    f.linestring_count = 0;

    if (!polygon.IsArray()) return;

    for (const auto& p_ring : polygon.GetArray())
    {
        if (!p_ring.IsArray()) return;

        uint32_t ring_point_count = 0;

        for (size_t i = 0; i < p_ring.Size() - 1; ++i)
        {
            const auto& p_pt = p_ring[i];
            tile.geometry.points.push_back(decode_geojson_point(p_pt));
            ring_point_count += 1;
        }

        f.point_count += ring_point_count;
        tile.geometry.linestring_sizes.push_back(ring_point_count);
        f.linestring_count += 1;
    }

    tile.geometry.features.push_back(f);
}

void decode_geojson_multi_polygon_geometry(
    MutableVectorTile& tile,
    const rapidjson::Value& coords_array)
{
    if (coords_array.IsArray())
    {
        for (const auto& coords : coords_array.GetArray())
        {
            decode_geojson_polygon_geometry(tile, coords);
        }
    }
}

bool decode_geojson_geometry(MutableVectorTile& tile, const rapidjson::Value& geometry)
{
    if (geometry["type"] == "Point")
    {
        decode_geojson_point_geometry(tile, geometry["coordinates"]);
    }
    else if (geometry["type"] == "MultiPoint")
    {
        decode_geojson_multi_point_geometry(tile, geometry["coordinates"]);
    }
    else if (geometry["type"] == "LineString")
    {
        decode_geojson_line_string_geometry(tile, geometry["coordinates"]);
    }
    else if (geometry["type"] == "MultiLineString")
    {
        decode_geojson_multi_line_string_geometry(tile, geometry["coordinates"]);
    }
    else if (geometry["type"] == "Polygon")
    {
        decode_geojson_polygon_geometry(tile, geometry["coordinates"]);
    }
    else if (geometry["type"] == "MultiPolygon")
    {
        decode_geojson_multi_polygon_geometry(tile, geometry["coordinates"]);
    }
    else if (geometry["type"] == "GeometryCollection")
    {
        const auto& geometries = geometry["geometries"];
        if (!geometries.IsArray()) return false;

        for (const auto& child_geometry : geometries.GetArray())
        {
            decode_geojson_geometry(tile, child_geometry);
        }
    }
    else
    {
        return false;
    }

    return true;
};

// For tiles with data from GeoJSON and Geobuf data.
// * Transform position from long-lat to web Mercator,
// * Compute bounding boxes,
// * Compute anchors.
void finalize_latlon_tile(MutableVectorTile& tile)
{
    auto features_data_opt = tile.geometry.features.data();
    auto points_data_opt = tile.geometry.points.data();
    auto sizes_data_opt = tile.geometry.linestring_sizes.data();
    if (!features_data_opt.has_value() || !points_data_opt.has_value()
        || !sizes_data_opt.has_value())
    {
        return;
    }
    auto features_data = features_data_opt.value();
    auto points_data = points_data_opt.value();
    auto sizes_data = sizes_data_opt.value();

    // Clamp all points to Web Mercator domain
    for (auto& point : points_data)
    {
        point.y = hrz::clamp(point.y, -hrz::MERCATOR_MAX_LAT_DEG, hrz::MERCATOR_MAX_LAT_DEG);
    }

    // Transform all points to web Mercator metres
    pl_transform_in_place_canonical(
        &hrz_proj::lonlat_deg_to_wmerc, points_data.size(), (double*)points_data.data());

    // Compute the bounding box of the points
    tile.geometry.bounds = lm::dbbox2::invalid();
    if (!points_data.empty())
    {
        for (const auto& point : points_data)
        {
            tile.geometry.bounds = lm::expand(tile.geometry.bounds, point.xy);
        }
    }

    // Compute all anchors
    for (auto& feature : features_data)
    {
        switch (feature.type)
        {
            case hrz_proto::VectorGeometryType::POINT_GEOMETRY:
            {
                feature.anchor = points_data[feature.first_point];
                feature.anchor_angle = 0.0F;
                break;
            }
            case hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY:
            {
                compute_polyline_anchor(tile, feature);
                break;
            }
            case hrz_proto::VectorGeometryType::POLYGON_GEOMETRY:
            {
                if (feature.linestring_count > 0)
                {
                    auto linestring = points_data.subspan(
                        feature.first_point, sizes_data[feature.first_linestring_size]);
                    feature.anchor = hrz::vector_data::compute_ring_average(linestring);
                    feature.anchor_angle = 0.0F;
                }
                else
                {
                    feature.anchor = lm::dvec3(std::numeric_limits<double>::quiet_NaN());
                    feature.anchor_angle = 0.0F;
                }
                break;
            }
            default: assert(!"Unhandled case");
        }
    }
}

bool decode_geojson(
    const hrz_jobs::EncodedVectorTile& encoded_tile,
    MutableVectorTile& decoded_tile,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    HRZ_SCOPED_SAMPLE("vector tiles job decode geojson");

    if (!std::holds_alternative<hrz::blobs::BlobHandle>(encoded_tile.data.data))
    {
        HRZ_LOG_ERROR("Expected blob as input for GeoJSON decoding");
        return false;
    }

    rapidjson::Document doc;
    {
        HRZ_SCOPED_SAMPLE("geojson parsing");
        auto raw_data = std::get<hrz::blobs::BlobHandle>(encoded_tile.data.data).get_data();
        doc.Parse((const char*)raw_data.data(), raw_data.size());
    }

    decoded_tile.coords = encoded_tile.coords;

    for (size_t i = 0; i < encoded_tile.attributes.size(); ++i)
    {
        decoded_tile.attributes.emplace_back(
            InitialFeatureCapacity, blob_allocator, resource_owner);
    }

    if (doc.HasParseError() || !doc.IsObject()) return false;

    std::vector<bool> attributes_with_errors(decoded_tile.attributes.size(), false);

    auto decode_feature = [&](const rapidjson::Value& feature)
    {
        if (strcmp(hrz::json::get_str_or(feature, "type", ""), "Feature") != 0) return;

        const auto& geometry = hrz::json::get_member_or_null(feature, "geometry");
        const auto& properties = hrz::json::get_member_or_null(feature, "properties");

        size_t new_features_count = 1;

        if (encoded_tile.decode_geometry)
        {
            if (!geometry.IsObject()) return;

            auto feature_count_before_opt = decoded_tile.geometry.features.size();
            if (!feature_count_before_opt.has_value()) return;
            size_t feature_count_before = feature_count_before_opt.value();

            if (!decode_geojson_geometry(decoded_tile, geometry)) return;

            auto feature_count_after_opt = decoded_tile.geometry.features.size();
            if (!feature_count_after_opt.has_value()) return;
            new_features_count = feature_count_after_opt.value() - feature_count_before;
        }

        if (encoded_tile.source_feature_id_attribute.has_value())
        {
            auto& feature_id_attribute =
                decoded_tile.attributes.at(encoded_tile.source_feature_id_attribute.value());
            auto transform =
                encoded_tile.attributes.at(encoded_tile.source_feature_id_attribute.value())
                    .transform;

            if (feature.HasMember("id"))
            {
                append_json_attribute_value(
                    feature_id_attribute, transform, feature["id"], new_features_count);
            }
            else
            {
                append_default_attribute_value(feature_id_attribute, new_features_count);
            }
        }

        if (!properties.IsObject())
        {
            for (size_t i = 0; i < decoded_tile.attributes.size(); ++i)
            {
                if (i != encoded_tile.source_feature_id_attribute)
                {
                    auto& decoded_attribute = decoded_tile.attributes.at(i);
                    append_default_attribute_value(decoded_attribute, new_features_count);
                }
            }

            return;
        }

        for (size_t i = 0; i < encoded_tile.attributes.size(); ++i)
        {
            if (i == encoded_tile.source_feature_id_attribute) continue;

            auto& encoded_attribute = encoded_tile.attributes.at(i);
            auto& decoded_attribute = decoded_tile.attributes.at(i);

            auto it = properties.FindMember(encoded_attribute.name.c_str());
            if (it != properties.MemberEnd())
            {
                if (!append_json_attribute_value(
                        decoded_attribute, encoded_attribute.transform, it->value,
                        new_features_count))
                {
                    attributes_with_errors[i] = true;
                }
            }
            else
            {
                append_default_attribute_value(decoded_attribute, new_features_count);
            }
        }
    };

    if (doc["type"] == "FeatureCollection")
    {
        const auto& features = doc["features"];
        if (features.IsArray())
        {
            reserve_attribute_space(decoded_tile.attributes, features.GetArray().Size());

            for (const auto& feature : features.GetArray())
            {
                decode_feature(feature);
            }
        }
    }
    else if (doc["type"] == "Feature")
    {
        reserve_attribute_space(decoded_tile.attributes, 1);
        decode_feature(doc);
    }
    else
    {
        if (!decode_geojson_geometry(decoded_tile, doc)) return false;
    }

    for (size_t i = 0; i < decoded_tile.attributes.size(); ++i)
    {
        if (attributes_with_errors[i])
        {
            HRZ_LOG_WARNING(
                "Error while reading values for attribute {} in tile {}-{}-{}",
                encoded_tile.attributes.at(i).id, encoded_tile.coords.lod, encoded_tile.coords.x,
                encoded_tile.coords.y);
        }
    }

    finalize_latlon_tile(decoded_tile);

    return true;
}

void decode_geobuf_geometry(
    MutableVectorTile& tile,
    const geobuf::Data::Geometry& geometry,
    bool has_z,
    double coord_factor)
{
    if (!tile.geometry.points.is_valid() || !tile.geometry.linestring_sizes.is_valid())
    {
        return;
    }

    uint32_t coords_per_position = has_z ? 3 : 2;
    uint32_t position_count = geometry.coords_size() / coords_per_position;
    uint32_t remaining_position_count = position_count;

    lm::ilvec3 current_encoded_position = {0, 0, 0};
    int current_position_index = 0;

    auto read_position = [&](bool is_delta)
    {
        auto x_index = current_position_index * coords_per_position + 0;
        auto y_index = x_index + 1;
        auto z_index = x_index + 2;

        lm::ilvec3 encoded_position;
        if (!is_delta)
        {
            encoded_position = {
                geometry.coords(x_index), geometry.coords(y_index),
                has_z ? geometry.coords(z_index) : 0
            };
        }
        else
        {
            encoded_position = {
                current_encoded_position.x + geometry.coords(x_index),
                current_encoded_position.y + geometry.coords(y_index),
                current_encoded_position.z + (has_z ? geometry.coords(z_index) : 0)
            };
        }
        current_encoded_position = encoded_position;
        current_position_index += 1;
        remaining_position_count -= 1;

        return lm::dvec3{
            (double)encoded_position.x * coord_factor, (double)encoded_position.y * coord_factor,
            (double)encoded_position.z * coord_factor
        };
    };

    auto add_position = [&](lm::dvec3 position) { tile.geometry.points.push_back(position); };

    auto add_point = [&]()
    {
        hrz::vector_data::VectorTileGeometry::Feature feature;
        feature.first_point = tile.geometry.points.size().value_or(0);
        feature.point_count = 1;
        feature.first_linestring_size = 0;
        feature.linestring_count = 0;
        feature.type = hrz_proto::VectorGeometryType::POINT_GEOMETRY;

        add_position(read_position(false));

        tile.geometry.features.push_back(feature);
    };

    auto add_polyline = [&](bool read_lengths, int first_linestring_length_index,
                            int linestring_count) -> bool
    {
        hrz::vector_data::VectorTileGeometry::Feature feature;
        feature.first_point = tile.geometry.points.size().value_or(0);
        feature.point_count = 0;
        feature.first_linestring_size = tile.geometry.linestring_sizes.size().value_or(0);
        feature.linestring_count = 0;
        feature.type = hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY;

        for (int i = 0; i < linestring_count; ++i)
        {
            uint32_t linestring_length = read_lengths
                ? geometry.lengths(first_linestring_length_index + i)
                : remaining_position_count;

            if (linestring_length <= remaining_position_count)
            {
                for (uint32_t j = 0; j < linestring_length; ++j)
                {
                    add_position(read_position(j != 0));
                }

                tile.geometry.linestring_sizes.push_back(linestring_length);
                feature.linestring_count += 1;
                feature.point_count += linestring_length;
            }
            else
            {
                return false;
            }
        }

        tile.geometry.features.push_back(feature);
        return true;
    };

    auto add_polygon = [&](bool read_lengths, int first_linestring_length_index,
                           int linestring_count) -> bool
    {
        hrz::vector_data::VectorTileGeometry::Feature feature;
        feature.first_point = tile.geometry.points.size().value_or(0);
        feature.point_count = 0;
        feature.first_linestring_size = tile.geometry.linestring_sizes.size().value_or(0);
        feature.linestring_count = 0;
        feature.type = hrz_proto::VectorGeometryType::POLYGON_GEOMETRY;

        for (int i = 0; i < linestring_count; ++i)
        {
            uint32_t ring_length = read_lengths
                ? geometry.lengths(first_linestring_length_index + i)
                : remaining_position_count;

            if (ring_length <= remaining_position_count)
            {
                for (uint32_t j = 0; j < ring_length; ++j)
                {
                    add_position(read_position(j != 0));
                }

                tile.geometry.linestring_sizes.push_back(ring_length);
                feature.linestring_count += 1;
                feature.point_count += ring_length;
            }
            else
            {
                return false;
            }
        }

        tile.geometry.features.push_back(feature);
        return true;
    };

    if (geometry.type() == geobuf::Data::Geometry::POINT)
    {
        if (position_count >= 1)
        {
            add_point();
        }
    }
    else if (geometry.type() == geobuf::Data::Geometry::MULTIPOINT)
    {
        for (uint32_t i = 0; i < position_count; ++i)
        {
            add_point();
        }
    }
    else if (geometry.type() == geobuf::Data::Geometry::LINESTRING)
    {
        bool has_lengths = geometry.lengths_size() >= 1;
        add_polyline(has_lengths, 0, 1);
    }
    else if (geometry.type() == geobuf::Data::Geometry::MULTILINESTRING)
    {
        bool has_lengths = geometry.lengths_size() >= 1;
        add_polyline(has_lengths, 0, has_lengths ? geometry.lengths_size() : 1);
    }
    else if (geometry.type() == geobuf::Data::Geometry::POLYGON)
    {
        bool has_lengths = geometry.lengths_size() >= 1;
        add_polygon(has_lengths, 0, has_lengths ? geometry.lengths_size() : 1);
    }
    else if (geometry.type() == geobuf::Data::Geometry::MULTIPOLYGON)
    {
        if (geometry.lengths_size() >= 1)
        {
            int polygon_count = geometry.lengths(0);
            int length_index = 1;

            for (int i = 0; i < polygon_count; ++i)
            {
                if (geometry.lengths_size() <= length_index + 1) break;

                int linestring_count = std::min(
                    (int)geometry.lengths(length_index),
                    geometry.lengths_size() - (length_index + 1));

                if (!add_polygon(true, length_index + 1, linestring_count)) break;

                length_index += 1 + linestring_count;
            }
        }
        else
        {
            add_polygon(false, 0, 1);
        }
    }
    else if (geometry.type() == geobuf::Data::Geometry::GEOMETRYCOLLECTION)
    {
        for (const auto& child_geometry : geometry.geometries())
        {
            decode_geobuf_geometry(tile, child_geometry, has_z, coord_factor);
        }
    }
}

bool decode_geobuf_attribute_value(
    hrz::vector_data::PackedAttributeValuesBuilder& decoded_attribute,
    hrz_proto::AttributeTransform transform,
    const geobuf::Data::Value& value,
    size_t feature_count)
{
    if (value.has_string_value())
    {
        append_attribute_value(decoded_attribute, transform, value.string_value(), feature_count);
    }
    else if (value.has_double_value())
    {
        append_attribute_value(decoded_attribute, transform, value.double_value(), feature_count);
    }
    else if (value.has_pos_int_value())
    {
        append_attribute_value(decoded_attribute, transform, value.pos_int_value(), feature_count);
    }
    else if (value.has_neg_int_value())
    {
        append_attribute_value(
            decoded_attribute, transform, -(int64_t)value.neg_int_value(), feature_count);
    }
    else if (value.has_bool_value())
    {
        append_attribute_value(decoded_attribute, transform, value.bool_value(), feature_count);
    }
    // We don't handle json_value.
    else
    {
        return false;
    }

    return true;
}

void decode_geobuf_attributes(
    const std::vector<hrz_jobs::AttributeModel>& encoded_attributes,
    const std::vector<std::optional<uint32_t>>& key_to_attribute,
    std::vector<hrz::vector_data::PackedAttributeValuesBuilder>& decoded_attributes,
    const geobuf::Data::Feature& feature_data,
    size_t feature_count,
    std::vector<bool>& attributes_with_values,
    std::vector<bool>& attributes_with_errors)
{
    for (int i = 0; i + 1 < feature_data.properties_size(); i += 2)
    {
        uint32_t key_index = feature_data.properties(i + 0);
        uint32_t value_index = feature_data.properties(i + 1);

        if (value_index >= (uint32_t)feature_data.values_size()) continue;
        if (key_index >= (uint32_t)key_to_attribute.size()) continue;
        auto attribute_index = key_to_attribute.at(key_index);
        if (!attribute_index.has_value()) continue;

        auto& decoded_attribute = decoded_attributes.at(*attribute_index);
        const auto& raw_value = feature_data.values(value_index);
        auto transform = encoded_attributes.at(*attribute_index).transform;

        if (decode_geobuf_attribute_value(decoded_attribute, transform, raw_value, feature_count))
        {
            attributes_with_values[*attribute_index] = true;
        }
        else
        {
            attributes_with_errors[*attribute_index] = true;
        }
    }
}

bool decode_geobuf(
    const hrz_jobs::EncodedVectorTile& encoded_tile,
    MutableVectorTile& decoded_tile,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    HRZ_SCOPED_SAMPLE("vector tiles job decode geobuf");

    if (!std::holds_alternative<hrz::blobs::BlobHandle>(encoded_tile.data.data))
    {
        HRZ_LOG_ERROR("Expected blob as input for Geobuf decoding");
        return false;
    }

    google::protobuf::Arena arena;
    auto geobuf = google::protobuf::Arena::Create<geobuf::Data>(&arena);
    bool success = false;
    {
        HRZ_SCOPED_SAMPLE("protobuf parsing");
        auto raw_data = std::get<hrz::blobs::BlobHandle>(encoded_tile.data.data).get_data();
        success = geobuf->ParseFromArray(raw_data.data(), raw_data.size());
    }

    decoded_tile.coords = encoded_tile.coords;

    for (size_t i = 0; i < encoded_tile.attributes.size(); ++i)
    {
        decoded_tile.attributes.emplace_back(
            InitialFeatureCapacity, blob_allocator, resource_owner);
    }

    if (!success)
    {
        HRZ_LOG_ERROR("Could not decode Geobuf file");
        return false;
    }

    std::vector<std::optional<uint32_t>> key_to_attribute;
    key_to_attribute.resize(geobuf->keys_size(), std::nullopt);

    for (int i = 0; i < geobuf->keys_size(); ++i)
    {
        const auto& key = geobuf->keys(i);

        for (size_t j = 0; j < encoded_tile.attributes.size(); ++j)
        {
            const auto& attribute = encoded_tile.attributes.at(j);
            if (j != encoded_tile.source_feature_id_attribute && attribute.name == key)
            {
                key_to_attribute[i] = {(uint32_t)j};
            }
        }
    }

    if (!(geobuf->dimensions() == 2 || geobuf->dimensions() == 3))
    {
        HRZ_LOG_ERROR("Unsupported dimension count in Geobuf file: {}", geobuf->dimensions());
        return false;
    }

    bool has_z = geobuf->dimensions() == 3;

    double coord_factor = 1.0 / std::pow(10.0, geobuf->precision());

    // Index in decoded tile to whether values were decoded for the feature.
    // This is reset for every feature.
    std::vector<bool> attributes_with_values(encoded_tile.attributes.size(), false);

    // True if a value was present, but wasn't of the expected type.
    // This is not reset for every feature.
    std::vector<bool> attributes_with_errors(encoded_tile.attributes.size(), false);

    auto decode_feature = [&](const geobuf::Data_Feature& feature)
    {
        size_t new_features_count = 1;

        if (encoded_tile.decode_geometry)
        {
            auto feature_count_before_opt = decoded_tile.geometry.features.size();
            if (!feature_count_before_opt.has_value()) return;
            size_t feature_count_before = feature_count_before_opt.value();

            decode_geobuf_geometry(decoded_tile, feature.geometry(), has_z, coord_factor);

            auto feature_count_after_opt = decoded_tile.geometry.features.size();
            if (!feature_count_after_opt.has_value()) return;
            new_features_count = feature_count_after_opt.value() - feature_count_before;
        }

        if (encoded_tile.source_feature_id_attribute.has_value())
        {
            auto& feature_id_attribute =
                decoded_tile.attributes.at(encoded_tile.source_feature_id_attribute.value());
            auto transform =
                encoded_tile.attributes.at(encoded_tile.source_feature_id_attribute.value())
                    .transform;

            if (feature.has_id())
            {
                append_attribute_value(
                    feature_id_attribute, transform, feature.id(), new_features_count);
            }
            else if (feature.has_int_id())
            {
                append_attribute_value(
                    feature_id_attribute, transform, feature.int_id(), new_features_count);
            }
            else
            {
                append_default_attribute_value(feature_id_attribute, new_features_count);
            }
        }

        for (size_t i = 0; i < attributes_with_values.size(); ++i)
        {
            attributes_with_values[i] = false;
        }

        decode_geobuf_attributes(
            encoded_tile.attributes, key_to_attribute, decoded_tile.attributes, feature,
            new_features_count, attributes_with_values, attributes_with_errors);

        for (size_t i = 0; i < attributes_with_values.size(); ++i)
        {
            if (i != encoded_tile.source_feature_id_attribute && !attributes_with_values[i])
            {
                auto& decoded_attribute = decoded_tile.attributes.at(i);

                append_default_attribute_value(decoded_attribute, new_features_count);
            }
        }
    };

    if (geobuf->has_feature_collection())
    {
        reserve_attribute_space(
            decoded_tile.attributes, geobuf->feature_collection().features_size());

        for (const auto& feature : geobuf->feature_collection().features())
        {
            decode_feature(feature);
        }
    }
    else if (geobuf->has_feature())
    {
        reserve_attribute_space(decoded_tile.attributes, 1);
        decode_feature(geobuf->feature());
    }
    else if (geobuf->has_geometry())
    {
        decode_geobuf_geometry(decoded_tile, geobuf->geometry(), has_z, coord_factor);
    }

    for (size_t i = 0; i < decoded_tile.attributes.size(); ++i)
    {
        if (attributes_with_errors[i])
        {
            HRZ_LOG_WARNING(
                "Error while reading values for attribute {} in tile {}-{}-{}",
                encoded_tile.attributes.at(i).id, encoded_tile.coords.lod, encoded_tile.coords.x,
                encoded_tile.coords.y);
        }
    }

    finalize_latlon_tile(decoded_tile);

    return true;
}

} // anonymous namespace

hrz_jobs::JobResult run(
    const hrz_jobs::EncodedVectorTile& encoded_tile,
    hrz::vector_data::DecodedVectorTile& decoded_tile,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("vector tiles job decode");

    auto blob_allocator = context.get_blob_allocator();
    auto owner = context.get_resource_owner();

    bool success = false;

    MutableVectorTile mutable_tile = {
        {},
        {
            lm::dbbox2::invalid(),
            hrz::BlobVector<hrz::vector_data::VectorTileGeometry::Feature>(
                blob_allocator, InitialFeatureCapacity),
            hrz::BlobVector<lm::dvec3>(blob_allocator, InitialPointCapacity),
            hrz::BlobVector<uint32_t>(blob_allocator, InitialLinestringSizeCapacity),
        },
        {}
    };
    mutable_tile.geometry.features.register_blob_metadata(
        "contents"_ss, "vector geometry features"_ss);
    mutable_tile.geometry.points.register_blob_metadata("contents"_ss, "vector geometry points"_ss);
    mutable_tile.geometry.linestring_sizes.register_blob_metadata(
        "contents"_ss, "vector geometry linestring sizes"_ss);

    mutable_tile.geometry.features.register_blob_owner(owner);
    mutable_tile.geometry.points.register_blob_owner(owner);
    mutable_tile.geometry.linestring_sizes.register_blob_owner(owner);

    switch (encoded_tile.data.format)
    {
        case hrz_proto::VectorDataFormat::MVT_VECTOR_DATA:
            success = decode_mvt(encoded_tile, mutable_tile, blob_allocator, owner);
            break;
        case hrz_proto::VectorDataFormat::GEOJSON_VECTOR_DATA:
            success = decode_geojson(encoded_tile, mutable_tile, blob_allocator, owner);
            break;
        case hrz_proto::VectorDataFormat::GEOBUF_VECTOR_DATA:
            success = decode_geobuf(encoded_tile, mutable_tile, blob_allocator, owner);
            break;
        default: assert(false && "Unhandled case"); break;
    }

    if (!success)
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    auto features_opt = mutable_tile.geometry.features.to_blob_array();
    auto points_opt = mutable_tile.geometry.points.to_blob_array();
    auto linestring_sizes_opt = mutable_tile.geometry.linestring_sizes.to_blob_array();
    if (!features_opt.has_value() || !points_opt.has_value() || !linestring_sizes_opt.has_value())
    {
        return hrz_jobs::JobResult::FAILURE;
    }

    decoded_tile.coords = mutable_tile.coords;
    decoded_tile.geometry.bounds = mutable_tile.geometry.bounds;
    decoded_tile.geometry.features = std::move(features_opt.value());
    decoded_tile.geometry.points = std::move(points_opt.value());
    decoded_tile.geometry.linestring_sizes = std::move(linestring_sizes_opt.value());

    assert(mutable_tile.attributes.size() == encoded_tile.attributes.size());
    for (size_t i = 0; i < mutable_tile.attributes.size(); ++i)
    {
        auto attribute_opt = mutable_tile.attributes[i].finalize(encoded_tile.attributes[i].id);
        if (!attribute_opt.has_value())
        {
            return hrz_jobs::JobResult::FAILURE;
        }

        decoded_tile.attributes.push_back(std::move(attribute_opt.value()));
    }

    {
        hrz::InlinedVector<hrz::vector_data::AttributeValues, 2> feature_id_attribute_values;
        for (size_t i = 0; i < encoded_tile.attributes.size(); ++i)
        {
            if (encoded_tile.attributes.at(i).is_feature_id)
            {
                feature_id_attribute_values.push_back(decoded_tile.attributes.at(i));
            }
        }

        auto feature_count = encoded_tile.decode_geometry
            ? decoded_tile.geometry.features.size()
            : (decoded_tile.attributes.empty() ? 0 : decoded_tile.attributes.front().values.size());
        hrz::BlobVector<hrz::vector_data::FeatureIdHash> hashes(blob_allocator, feature_count);
        hashes.register_blob_metadata("contents"_ss, "feature ID hashes"_ss);
        hashes.register_blob_owner(context.get_resource_owner());
        hashes.resize(feature_count);
        auto hashes_opt = hashes.to_blob_array();
        if (!hashes_opt.has_value())
        {
            return hrz_jobs::JobResult::FAILURE;
        }

        auto feature_ids = hrz::vector_data::FeatureIds::make(
            feature_id_attribute_values, std::move(hashes_opt.value()));
        if (feature_ids.has_value())
        {
            decoded_tile.feature_ids = std::move(feature_ids.value());
        }
        else
        {
            return hrz_jobs::JobResult::FAILURE;
        }
    }

#ifndef NDEBUG
    {
        auto feature_count = encoded_tile.decode_geometry
            ? decoded_tile.geometry.features.size()
            : (decoded_tile.attributes.empty() ? 0 : decoded_tile.attributes.front().values.size());
        assert(decoded_tile.feature_ids.size() == feature_count);
        for (const auto& attribute : decoded_tile.attributes)
        {
            assert(attribute.values.size() == feature_count);
        }
    }
#endif

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace hrz_jobs::decode_vector_tile
