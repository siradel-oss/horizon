// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_vector.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/vector_data//tile_geometry.h"
#include "hrz/fnd/class.h"
#include "hrz/fnd/function_ref.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

namespace hrz::vector_data
{

// Use this class to build a VectorTileGeometry object.
//
// Each call to a top-level add_* method adds exactly one feature to the tile,
// no matter what kind of geometry it is, except when called inside an
// add_geometry_collection callback, in which case it adds a feature to the
// collection instead of the tile.
class VectorTileGeometryBuilder
{
    lm::dbbox2 _bounds = lm::dbbox2::invalid();
    hrz::BlobVector<VectorTileGeometry::Feature> _features;
    hrz::BlobVector<VectorTileGeometry::GeometryPart> _geometry_parts;
    hrz::BlobVector<lm::dvec3> _points;
    hrz::BlobVector<uint32_t> _linestring_sizes;

    VectorTileGeometry::Feature* _current_feature = nullptr;

    // Create a new geometry part of the given type with the current point, linestring,
    // and polygon ring count indices.
    // Returns the index of the geometry part that was allocated,
    // or nullopt if no geometry part was allocated.
    std::optional<uint32_t> allocate_current_geometry_part(hrz_proto::VectorGeometryType type);

    // Returns the index of the geometry part that was added to the feature,
    // or nullopt if no geometry part was added.
    // If called on a feature that already has geometry parts, nothing happens.
    std::optional<uint32_t> move_simple_feature_to_geometry_part(
        VectorTileGeometry::Feature* feature);

    // Adds a new geometry part to the given feature, and returns a pointer to it.
    // If the feature was "simple", is it converted to a collection.
    VectorTileGeometry::GeometryPart* add_geometry_part_to_feature(
        VectorTileGeometry::Feature* feature,
        hrz_proto::VectorGeometryType);

    // Allocates a new feature and returns a pointer to it.
    // The initial feature is empty and of undefined type.
    // All new features start their life as a "simple" feature and become a collection
    // only when more than one geometry part is added to them, even in the case
    // of a geometry collection feature.
    VectorTileGeometry::Feature* allocate_feature();

    // Gets the current feature, allocating a new one if necessary.
    // This new feature will not be made the current one automatically.
    VectorTileGeometry::Feature* current_or_new_feature();

    void add_linestring_to_geometry_part(
        VectorTileGeometry::GeometryPart* part,
        uint32_t linestring_size);

public:
    explicit VectorTileGeometryBuilder(
        hrz::BlobAllocator* allocator,
        size_t initial_feature_capacity = 0,
        size_t initial_point_capacity = 0,
        size_t initial_linestring_size_capacity = 0);
    HRZ_DELETE_COPY_MOVE(VectorTileGeometryBuilder);
    ~VectorTileGeometryBuilder() = default;

    void register_blobs_owner(const hrz::monitoring::ResourceOwner& owner);

    // Finalizes the tile geometry and returns it.
    // This includes computing the tile bounds, and the feature anchors.
    std::optional<VectorTileGeometry> finalize() &&;

    class PointsBuilder
    {
        VectorTileGeometryBuilder* _parent;
        VectorTileGeometry::Feature* _feature;
        uint32_t _added_points_count = 0;

        PointsBuilder(VectorTileGeometryBuilder* parent, VectorTileGeometry::Feature* feature) :
            _parent(parent), _feature(feature)
        {
        }

        [[nodiscard]] bool finish();

        friend class VectorTileGeometryBuilder;

    public:
        HRZ_DELETE_COPY(PointsBuilder);
        HRZ_DEFAULT_MOVE(PointsBuilder);
        ~PointsBuilder();

        void add_point(const lm::dvec3&);
    };

    class PolylinesBuilder
    {
        VectorTileGeometryBuilder* _parent;
        VectorTileGeometry::Feature* _feature;
        VectorTileGeometry::GeometryPart* _part = nullptr;
        uint32_t _current_linestring_size = 0;

        PolylinesBuilder(VectorTileGeometryBuilder* parent, VectorTileGeometry::Feature* feature) :
            _parent(parent), _feature(feature)
        {
        }

        [[nodiscard]] bool finish();

        friend class VectorTileGeometryBuilder;

    public:
        HRZ_DELETE_COPY(PolylinesBuilder);
        HRZ_DEFAULT_MOVE(PolylinesBuilder);
        ~PolylinesBuilder();

        void add_point(const lm::dvec3&);

        // Will be called automatically when the PolylinesBuilder is destroyed if
        // you did not call it yourself.
        [[nodiscard]] bool end_linestring();
    };

    class PolygonsBuilder
    {
        VectorTileGeometryBuilder* _parent;
        VectorTileGeometry::Feature* _feature;
        VectorTileGeometry::GeometryPart* _part = nullptr;

        uint32_t _current_ring_size = 0;
        uint32_t _current_ring_count = 0;
        uint32_t _current_polygon_count = 0;

        PolygonsBuilder(VectorTileGeometryBuilder* parent, VectorTileGeometry::Feature* feature) :
            _parent(parent), _feature(feature)
        {
        }

        [[nodiscard]] bool finish();

        friend class VectorTileGeometryBuilder;

    public:
        HRZ_DELETE_COPY(PolygonsBuilder);
        HRZ_DEFAULT_MOVE(PolygonsBuilder);
        ~PolygonsBuilder();

        void add_point(const lm::dvec3&);

        // Will be called automatically and the end of a polygon
        // if you did not call it yourself.
        [[nodiscard]] bool end_ring();

        // Ends the current ring as the exterior ring of a new polygon. If a polygon was
        // being built, it is ended first, as if end_polygon() had been called before adding
        // this ring's points. This is useful when whether a ring is an exterior ring or a hole
        // is only known once the ring is complete, e.g. from its winding order.
        // Does nothing if the current ring has no points.
        [[nodiscard]] bool end_exterior_ring();

        // Will be called automatically when the PolygonsBuilder is destroyed
        // if you did not call it yourself.
        [[nodiscard]] bool end_polygon();

        [[nodiscard]]
        std::span<const lm::dvec3> current_ring_points() const;
    };

    [[nodiscard]]
    bool add_points_feature(hrz::function_ref<bool(PointsBuilder&)> callback);

    [[nodiscard]]
    bool add_polylines_feature(hrz::function_ref<bool(PolylinesBuilder&)> callback);

    [[nodiscard]]
    bool add_polygons_feature(hrz::function_ref<bool(PolygonsBuilder&)> callback);

    enum class EmptyCollection
    {
        // An empty collection is added as a feature without geometry.
        Keep,
        // An empty collection doesn't add any feature.
        // Only applies to top-level collections. Use feature_count() to know whether a
        // feature was added.
        Discard,
    };

    [[nodiscard]]
    bool add_geometry_collection(
        hrz::function_ref<bool(VectorTileGeometryBuilder&)> callback,
        EmptyCollection empty_collection = EmptyCollection::Keep);

    [[nodiscard]]
    std::optional<uint32_t> feature_count() const
    {
        return _features.size();
    }

    [[nodiscard]]
    bool process_points_in_place(hrz::function_ref<bool(std::span<lm::dvec3>)> callback);
};

} // namespace hrz::vector_data
