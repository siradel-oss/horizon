#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/geo.h"
#include "hrz/common/maths.h"
#include "hrz/common/vector_data.h"
#include "hrz/fnd/variant.h"

#include <lin_maths.h>

#include <optional>
#include <span>
#include <string>

namespace hrz::three_d_tiles
{
enum class RefinementType
{
    ADD,
    REPLACE,
};

/**
 * Default volume is a sphere with a negative radius (i.e. no volume).
 */
struct BoundingVolume
{
    using Box = hrz::OrientedBBox3<double>;
    using Sphere = hrz::BSphere<double>;

    struct Region
    {
        GeoVolumeBounds bounds;

        std::optional<Box> box;
        std::optional<Sphere> sphere;
    };

    lm::dvec3 center = {0, 0, 0};
    double radius = -1;
    std::variant<Sphere, Region, Box> volume;
};

/**
 * Precompute some structures, including center and radius,
 * to make spatial computations faster and/or more precise.
 */
void optimize(BoundingVolume& volume);

/**
 * Return the distance between the volume and the ECEF position.
 */
double distance(const BoundingVolume& volume, const lm::dvec3& ecef_pos);

/**
 * Return true if the bounding volume is intersects the space subset defined
 * by the points "below" each plane. (The points whose scalar product with
 * the vector starting from any point of the plane, to which the normal of
 * the plane is added, is negative or zero.)
 *
 * Planes are defined as:
 *     lm::dvec4 p = {a, b, c, d} <-> ax + by + cz + d = 0
 *
 * Planes must be normalised (i.e. norm(xyz) == 1).
 *
 * Additionally, vertices can be passed to the function, in which case an
 * additional test is performed, where if there is a separating axis
 * between the volume and the set of vertices, the volume is considered
 * not to intersect the space subset.
 *
 * See https://iquilezles.org/articles/frustumcorrect/
 *
 * (This additional test is still not fully exhaustive for boxes. But it
 * shouldn't matter too much. See https://stackoverflow.com/a/31789828)
 */
bool intersects_space_subset(
    const BoundingVolume& volume,
    std::span<const lm::dvec4> planes,
    std::span<const lm::dvec3> vertices);

BoundingVolume transform_bounding_volume(
    const BoundingVolume& volume,
    const lm::dmat4& transform,
    const lm::dmat4& root_transform);

size_t compute_hash(const BoundingVolume& volume);

// Computes a point whose occlusion by the horizon implies the occlusion of the whole volume.
// This cannot always be computed, in which case the volume cannot be occluded by the horizon.
std::optional<lm::dvec3> compute_horizon_occlusion_point(const BoundingVolume& volume);

struct EncodedThreeDTilesTileset
{
    hrz::blobs::BlobHandle raw_json;
    lm::dmat4 transform;
    uint32_t root_depth;
};

struct ThreeDTilesTilesetDescriptor
{
    // SIRADEL_range_request extension
    struct Range
    {
        uint64_t offset;
        uint64_t length;
        std::optional<std::string> mime_type;
    };

    struct Tile
    {
        lm::dmat4 transform;
        BoundingVolume bounding_volume;
        double geometric_error;
        RefinementType refinement_type;
        std::string uri;
        std::optional<Range> range;
        std::optional<BoundingVolume> content_bounding_volume;
        std::optional<BoundingVolume> viewer_bounding_volume;
        uint32_t parent_index;
        uint32_t child_count;
        uint32_t first_child_link_index;
        uint32_t depth;
    };

    std::vector<Tile> tiles;
    uint32_t root_tile_index;
    std::vector<uint32_t> child_links;
    double geometric_error;
    bool allow_gltf_content;
};

enum class AttributeComponentType
{
    BYTE,
    UNSIGNED_BYTE,
    SHORT,
    UNSIGNED_SHORT,
    INT,
    UNSIGNED_INT,
    INT64,
    UNSIGNED_INT64,
    FLOAT,
    DOUBLE,
    STRING,
};

struct AttributeConfig
{
    struct BatchTable
    {
        bool is_feature_id;
    };

    struct VectorDataLayer
    {
    };

    struct BatchClassId
    {
        bool is_feature_id;
    };

    struct BatchClassName
    {
        bool is_feature_id;
    };

    using Source = std::variant<BatchTable, VectorDataLayer, BatchClassId, BatchClassName>;

    std::string name;
    Source source;
    uint32_t attribute_id;
    int style_id;
    hrz_proto::AttributeTransform transform;

    constexpr bool has_batch_table_source() const
    {
        return std::holds_alternative<BatchTable>(source);
    }

    constexpr bool has_batch_class_id_source() const
    {
        return std::holds_alternative<BatchClassId>(source);
    }

    constexpr bool has_batch_class_name_source() const
    {
        return std::holds_alternative<BatchClassName>(source);
    }

    constexpr bool has_vector_data_layer_source() const
    {
        return std::holds_alternative<VectorDataLayer>(source);
    }

    constexpr bool is_feature_id_attribute() const
    {
        if (has_batch_table_source())
        {
            return std::get<BatchTable>(source).is_feature_id;
        }
        else if (has_batch_class_id_source())
        {
            return std::get<BatchClassId>(source).is_feature_id;
        }
        else if (has_batch_class_name_source())
        {
            return std::get<BatchClassName>(source).is_feature_id;
        }
        else
        {
            return false;
        }
    }
};

struct EncodedBatchTable
{
    uint32_t batch_length;
    hrz::blobs::BlobHandle json_data;
    hrz::blobs::BlobHandle bin_data;
    std::vector<AttributeConfig> attributes;
};

struct DecodedBatchTable
{
    hrz::vector_data::FeatureIds batches_to_feature_ids;
    std::vector<std::optional<hrz::vector_data::AttributeValues>> attribute_values;
};

std::optional<AttributeComponentType> component_type_from_string(std::string_view str);
} // namespace hrz::three_d_tiles

namespace std
{
template<>
struct hash<hrz::three_d_tiles::ThreeDTilesTilesetDescriptor::Range>
{
    size_t operator()(const hrz::three_d_tiles::ThreeDTilesTilesetDescriptor::Range& range) const
    {
        return hrz::hash_values(range.length, range.offset, range.mime_type);
    }
};
} // namespace std
