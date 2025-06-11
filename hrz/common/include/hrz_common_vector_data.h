#pragma once

#include "hrz_common_attributes.h"
#include "hrz_common_blob_allocator.h"
#include "hrz_common_blob_array.h"
#include "hrz_common_geo.h"
#include "hrz_common_tile_coords.h"

#include <hrz_fnd_bit_cast.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_variant.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace vector_tile
{
class Tile;
}

namespace hrz::vector_data
{
struct AttributeModel
{
    uint32_t id;
    std::string name;
    hrz_proto::AttributeTransform transform;
    bool is_feature_id;
};

struct ParsedMvt
{
    std::shared_ptr<google::protobuf::Arena> arena;
    vector_tile::Tile* tile;
};

struct VectorDataPackage
{
    // BlobHandle for GeoJSON and Geobuf.
    // ParsedMvt for MVT.
    std::variant<hrz::blobs::BlobHandle, ParsedMvt> data;

    hrz_proto::VectorDataFormat format;
};

struct EncodedVectorTile
{
    // In XYZ format.
    hrz::TileCoords coords;
    VectorDataPackage data;
    std::string layer_name;
    std::optional<size_t> source_feature_id_attribute;
    std::vector<AttributeModel> attributes;
};

struct VectorTileGeometry
{
    struct Feature
    {
        // Index of first point in "points".
        // Coordinates must be in Web Mercator.
        uint32_t first_point;

        // Number of points.
        // 1 for points.
        // Sum of points in each linestring for polygons and polylines.
        // Linestrings must not be closed.
        uint32_t point_count;

        hrz_proto::VectorGeometryType type;

        // Index of first linestring size in "sizes".
        // A linestring size is the number of points for each linestring.
        // Linestrings (also called rings for polygons) must not be closed.
        // For polygons, exterior linestring must be counter-clockwise, and interior ones must
        // be clockwise.
        // This is used only for polygons and polylines.
        uint32_t first_linestring_size;

        // Number of linestrings for polygons and polylines.
        // This is used only for polygons and polylines.
        uint32_t linestring_count;

        // Anchor of the feature in Web Mercator.
        // For points, these are the same as the x/y/z coords of the point.
        lm::dvec3 anchor;

        // Angle of the feature's anchor in radians, counter-clockwise, 0 is east.
        // This is only non-zero for polylines.
        float anchor_angle;
    };

    // Bounds of the geometry, in Web Mercator metres.
    lm::dbbox2 bounds;

    hrz::BlobArray<Feature> features;

    // All coordinates must be in Web Mercator (EPSG 3857)
    hrz::BlobArray<lm::dvec3> points;

    // Linestring sizes for polygon and polyline features.
    hrz::BlobArray<uint32_t> linestring_sizes;

    // True if the four children tiles, if they exist, would not provide more information
    // than this tile.
    bool has_full_detail;
};

using FeatureIdHash = uint64_t;

struct FeatureIds;

struct FeatureId
{
    struct Value
    {
        uint32_t attribute_id;
        OwnedAttributeValue value;
    };

    struct Builder
    {
        void add_value(uint32_t attribute_id, OwnedAttributeValue);
        FeatureId build();

    private:
        hrz::InlinedVector<Value, 2> _values;
    };

    bool operator==(const FeatureId& other) const { return other._hash == _hash; }

    size_t value_count() const { return _values.size(); }

    Value value_at(size_t i) const { return _values.at(i); }

    bool is_null() const { return _values.empty(); }

    FeatureIdHash hash() const { return _hash; }

    static FeatureId from_proto(const hrz_proto::FeatureId&);
    void to_proto(hrz_proto::FeatureId* dst) const;

    template<typename H>
    friend H AbslHashValue(H h, const FeatureId& id)
    {
        return H::combine(std::move(h), id._hash);
    }

private:
    hrz::InlinedVector<Value, 2> _values;
    FeatureIdHash _hash;

    friend struct FeatureIds;
};

struct FeatureIds
{
    using Hash = uint64_t;

    // The array for the hashes needs to be allocated and have the
    // same size as the attribute values.
    // The data inside does not matter, the hashes are computed by
    // this function.
    // This function takes the ownership of the array.
    static std::optional<FeatureIds> make(
        gsl::span<AttributeValues> attribute_values,
        BlobArray<FeatureIdHash> hashes_array);

    Hash compute_hash();

    hrz::BlobArray<FeatureIdHash> hashes() const { return _hashes; }

    size_t size_bytes() const;

    size_t size() const { return _size; }

    bool empty() const { return _size == 0; }

    bool has_any_attribute() const { return _values.size() > 0; }

    bool has_attribute(uint32_t attribute_id) const;
    bool has_same_attributes(const FeatureIds&) const;

    bool contains(const FeatureId& feature_id) const;
    FeatureId at(size_t i) const;

    void to_proto(google::protobuf::RepeatedPtrField<hrz_proto::FeatureId>* dst) const;

private:
    size_t _size = 0;
    hrz::InlinedVector<AttributeValues, 2> _values;
    hrz::BlobArray<FeatureIdHash> _hashes;
    std::optional<Hash> _hash;
};

struct DecodedVectorTile
{
    hrz::TileCoords coords;

    // There can be multiple features with the same id.
    // This is the case for multipoints, multipolylines and multipolygons.
    FeatureIds feature_ids;

    VectorTileGeometry geometry;
    std::vector<AttributeValues> attributes;
};

struct AabbTree
{
    struct Node
    {
        lm::dbbox2 bbox;
        bool has_children;

        union
        {
            uint32_t child_indices[2];
            uint32_t feature_index;
        };
    };

    hrz::BlobArray<Node> aabb_tree;
    hrz::GeoBounds bounds;
};

struct ExtractedVectorTileFeatureInfo
{
    uint32_t source_data_index;
    lm::dbbox2 bbox;
};

struct VectorTileExtractionParams
{
    hrz::TileCoords coords;
    DecodedVectorTile source_data;
    AabbTree aabb_tree;
    float tolerance{};
    bool include_clip_margin{};
    hrz::GeoBounds bounds;
};

struct UnsortedVectorData
{
    // This is the reference feature order.
    // The list can contain duplicates, that must be respected.
    FeatureIds reference_feature_ids;

    // The feature order of the geometry and attributes values.
    FeatureIds feature_ids;

    std::optional<VectorTileGeometry> geometry;

    // These attribute values will be sorted.
    std::vector<AttributeValues> attributes;
};

struct SortedVectorData
{
    std::optional<VectorTileGeometry> geometry;
    std::vector<AttributeValues> attributes;
};

struct RawClientVectorData
{
    hrz_proto::VectorDataRequestResponse client_data;
    bool expects_geometry;
    std::vector<AttributeModel> attributes;
};

lm::dvec3 compute_ring_average(gsl::span<const lm::dvec3> linestring);

void compute_linestring_middle_and_angle(
    gsl::span<const lm::dvec3> points,
    gsl::span<const uint32_t> linestring_sizes,
    lm::dvec3* middle,
    float* angle);

// The minimal *squared* distance required between a point and its projection on the "core line"
// of a polyline (which goes from first to last point) for the point to be considered not
// simplifiable at the given level of detail.
double compute_vector_tile_tolerance(uint32_t lod);

} // namespace hrz::vector_data
