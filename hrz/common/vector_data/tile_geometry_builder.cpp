// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/vector_data/tile_geometry_builder.h"

#include "hrz/fnd/defer.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

using Feature = hrz::vector_data::VectorTileGeometry::Feature;
using FeatureFlags = hrz::vector_data::VectorTileGeometry::FeatureFlags;
using GeometryPart = hrz::vector_data::VectorTileGeometry::GeometryPart;

namespace
{

using namespace hrz::vector_data;

void compute_anchors(
    hrz::BlobArray<Feature>& features,
    const hrz::BlobArray<GeometryPart>& geometry_parts,
    const hrz::BlobArray<lm::dvec3>& points,
    const hrz::BlobArray<uint32_t>& linestring_sizes)
{
    auto features_data = features.get_mutable_data();
    const auto geometry_parts_data = geometry_parts.get_cdata();
    const auto points_data = points.get_cdata();
    const auto linestring_sizes_data = linestring_sizes.get_cdata();

    const auto features_span = features_data.as_span();
    const auto geometry_parts_span = geometry_parts_data.as_span();
    const auto points_span = points_data.as_span();
    const auto linestring_sizes_span = linestring_sizes_data.as_span();

    for (auto& feature : features_span)
    {
        compute_feature_anchor(feature, points_span, linestring_sizes_span, geometry_parts_span);
    }
}

} // anonymous namespace

namespace hrz::vector_data
{

VectorTileGeometryBuilder::VectorTileGeometryBuilder(
    hrz::BlobAllocator* allocator,
    size_t initial_feature_capacity,
    size_t initial_point_capacity,
    size_t initial_linestring_size_capacity) :
    _features(allocator, initial_feature_capacity),
    _geometry_parts(allocator, initial_feature_capacity),
    _points(allocator, initial_point_capacity),
    _linestring_sizes(allocator, initial_linestring_size_capacity)
{
    _features.register_blob_metadata("contents"_ss, "vector geometry features"_ss);
    _geometry_parts.register_blob_metadata("contents"_ss, "vector geometry geometry parts"_ss);
    _points.register_blob_metadata("contents"_ss, "vector geometry points"_ss);
    _linestring_sizes.register_blob_metadata("contents"_ss, "vector geometry linestring sizes"_ss);
}

void VectorTileGeometryBuilder::register_blobs_owner(const hrz::monitoring::ResourceOwner& owner)
{
    _features.register_blob_owner(owner);
    _geometry_parts.register_blob_owner(owner);
    _points.register_blob_owner(owner);
    _linestring_sizes.register_blob_owner(owner);
}

std::optional<VectorTileGeometry> VectorTileGeometryBuilder::finalize() &&
{
    auto features_opt = _features.to_blob_array();
    auto geometry_parts_opt = _geometry_parts.to_blob_array();
    auto points_opt = _points.to_blob_array();
    auto linestring_sizes_opt = _linestring_sizes.to_blob_array();

    if (!features_opt || !geometry_parts_opt || !points_opt || !linestring_sizes_opt)
    {
        return std::nullopt;
    }

    compute_anchors(*features_opt, *geometry_parts_opt, *points_opt, *linestring_sizes_opt);

    {
        auto points = points_opt->get_cdata();
        lm::dbbox2 bounds = lm::dbbox2::invalid();
        for (const auto& point : points)
        {
            bounds = lm::expand(bounds, point.xy);
        }

        _bounds = bounds;
    }

    return VectorTileGeometry{
        .bounds = _bounds,
        .features = std::move(*features_opt),
        .geometry_parts = std::move(*geometry_parts_opt),
        .points = std::move(*points_opt),
        .linestring_sizes = std::move(*linestring_sizes_opt),
        .has_full_detail = false
    };
}

std::optional<uint32_t> VectorTileGeometryBuilder::allocate_current_geometry_part(
    hrz_proto::VectorGeometryType type)
{
    _geometry_parts.push_back(
        GeometryPart{
            .type = type,
            .first_point = (uint32_t)_points.size().value_or(0),
            .point_count = 0,
            .first_linestring_size = (uint32_t)_linestring_sizes.size().value_or(0),
            .linestring_count = 0,
        });

    if (auto part_index = _geometry_parts.size(); part_index.has_value())
    {
        return (uint32_t)part_index.value() - 1;
    }

    return std::nullopt;
}

std::optional<uint32_t> VectorTileGeometryBuilder::move_simple_feature_to_geometry_part(
    VectorTileGeometry::Feature* feature)
{
    assert(feature && !feature->flags.is_collection && feature->geometry_part_count > 0);

    auto type = feature->flags.primitive_geometry_type();
    if (!type) return std::nullopt;

    auto part_index_opt = allocate_current_geometry_part(type.value());
    if (!part_index_opt) return std::nullopt;

    auto part_index = part_index_opt.value();

    auto* part = _geometry_parts.at(part_index);
    part->first_point = feature->first_geometry_part;
    part->point_count = feature->geometry_part_count;

    // When there is only one linestring or polygon, we don't use the linestring_sizes array. This
    // is because point_count is enough to describe the geometry, and we don't need to store the
    // sizes in a separate array. This means however that the first time we add the second
    // linestring, we need to also add the first to those arrays.
    if (type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY
        || type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY)
    {
        part->linestring_count = 1;
    }

    feature->first_geometry_part = part_index;
    feature->geometry_part_count = 1;
    feature->flags = FeatureFlags::make_collection();
    feature->flags.collection_geometry_types = (uint8_t)(1 << type.value());

    return part_index;
}

void VectorTileGeometryBuilder::add_linestring_to_geometry_part(
    VectorTileGeometry::GeometryPart* part,
    uint32_t linestring_size)
{
    assert(
        part
        && (part->type == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY
            || part->type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY));

    if (part->linestring_count > 0)
    {
        // If there was already exactly one linestring, we did not use the
        // linestring_sizes array, so we need to add the first linestring size now.
        if (part->linestring_count == 1)
        {
            _linestring_sizes.push_back(part->point_count);
        }

        _linestring_sizes.push_back(linestring_size);
    }
    else
    {
        assert(part->point_count == 0);
    }

    part->linestring_count += 1;
    part->point_count += linestring_size;
}

VectorTileGeometry::GeometryPart* VectorTileGeometryBuilder::add_geometry_part_to_feature(
    VectorTileGeometry::Feature* feature,
    hrz_proto::VectorGeometryType geometry_type)
{
    assert(feature && (feature->flags.is_collection || feature->geometry_part_count > 0));

    if (!feature->flags.is_collection)
    {
        move_simple_feature_to_geometry_part(feature);
    }
    assert(feature->flags.is_collection);

    auto part_index_opt = allocate_current_geometry_part(geometry_type);
    if (!part_index_opt) return nullptr;

    feature->flags.collection_geometry_types |= (uint8_t)(1 << geometry_type);
    feature->geometry_part_count += 1;
    return _geometry_parts.at(part_index_opt.value());
}

VectorTileGeometry::Feature* VectorTileGeometryBuilder::allocate_feature()
{
    _features.push_back(
        Feature{
            .first_geometry_part = 0,
            .geometry_part_count = 0,
            .anchor = {},
            .anchor_angle = 0.0F,
            .flags = FeatureFlags::make_collection()
        });

    if (auto feature_index = _features.size(); feature_index.has_value())
    {
        return _features.at(feature_index.value() - 1);
    }

    return nullptr;
}

VectorTileGeometry::Feature* VectorTileGeometryBuilder::current_or_new_feature()
{
    auto* feature = _current_feature;
    if (!feature)
    {
        feature = allocate_feature();
    }

    if (feature && feature->geometry_part_count == 0)
    {
        feature->first_geometry_part = (uint32_t)_points.size().value_or(0);
        feature->flags = FeatureFlags::make_collection();
    }

    return feature;
}

bool VectorTileGeometryBuilder::add_points_feature(hrz::function_ref<bool(PointsBuilder&)> callback)
{
    auto* feature = current_or_new_feature();
    if (feature)
    {
        PointsBuilder points_builder(this, feature);
        if (!callback(points_builder)) return false;
        return points_builder.finish();
    }
    else
    {
        return false;
    }
}

bool VectorTileGeometryBuilder::add_polylines_feature(
    hrz::function_ref<bool(PolylinesBuilder&)> callback)
{
    auto* feature = current_or_new_feature();
    if (feature)
    {
        PolylinesBuilder polylines_builder(this, feature);
        if (!callback(polylines_builder)) return false;
        return polylines_builder.finish();
    }
    else
    {
        return false;
    }
}

bool VectorTileGeometryBuilder::add_polygons_feature(
    hrz::function_ref<bool(PolygonsBuilder&)> callback)
{
    auto* feature = current_or_new_feature();
    if (feature)
    {
        PolygonsBuilder polygons_builder(this, feature);
        if (!callback(polygons_builder)) return false;
        return polygons_builder.finish();
    }
    else
    {
        return false;
    }
}

bool VectorTileGeometryBuilder::add_geometry_collection(
    hrz::function_ref<bool(VectorTileGeometryBuilder&)> callback,
    EmptyCollection empty_collection)
{
    auto* old_current_feature = _current_feature;
    _current_feature = current_or_new_feature();
    HRZ_DEFER[this, old_current_feature]()
    {
        _current_feature = old_current_feature;
    };

    if (_current_feature)
    {
        if (!callback(*this)) return false;

        const bool is_top_level = old_current_feature == nullptr;
        if (empty_collection == EmptyCollection::Discard && is_top_level
            && _current_feature->geometry_part_count == 0)
        {
            const auto feature_count_opt = _features.size();
            if (!feature_count_opt.has_value()) return false;

            // The collection's feature is necessarily the last one, since nothing inside a
            // collection allocates new features.
            assert(_features.at(feature_count_opt.value() - 1) == _current_feature);
            _features.resize(feature_count_opt.value() - 1);
        }

        return true;
    }
    else
    {
        return false;
    }
}

bool VectorTileGeometryBuilder::process_points_in_place(
    hrz::function_ref<bool(std::span<lm::dvec3>)> callback)
{
    auto points_data_opt = _points.data();
    if (!points_data_opt) return false;

    const auto points_span = *points_data_opt;
    if (!callback(points_span)) return false;

    return true;
}

void VectorTileGeometryBuilder::PointsBuilder::add_point(const lm::dvec3& point)
{
    _parent->_points.push_back(point);
    _added_points_count += 1;
}

bool VectorTileGeometryBuilder::PointsBuilder::finish()
{
    if (_added_points_count == 0) return true;

    HRZ_DEFER[this]()
    {
        _added_points_count = 0;
    };

    auto first_point_index_opt = _parent->_points.size();
    if (!first_point_index_opt.has_value()) return false;

    const auto first_point_index = (uint32_t)(first_point_index_opt.value() - _added_points_count);

    // If we already have geometry parts, this is already a non-empty simple feature, or
    // a non-empty collection. In either case, we need to add a new geometry part for the points.
    if (_feature->geometry_part_count > 0)
    {
        auto* part = _parent->add_geometry_part_to_feature(
            _feature, hrz_proto::VectorGeometryType::POINT_GEOMETRY);
        if (!part) return false;

        part->first_point = first_point_index;
        part->point_count = _added_points_count;
    }
    // Otherwise we can simply reuse the simple feature to store out points.
    else
    {
        _feature->flags =
            FeatureFlags::make_simple_feature(hrz_proto::VectorGeometryType::POINT_GEOMETRY);
        _feature->first_geometry_part = first_point_index;
        _feature->geometry_part_count = _added_points_count;
    }

    return true;
}

VectorTileGeometryBuilder::PointsBuilder::~PointsBuilder()
{
    assert(_added_points_count == 0);
}

void VectorTileGeometryBuilder::PolylinesBuilder::add_point(const lm::dvec3& point)
{
    _parent->_points.push_back(point);
    _current_linestring_size += 1;
}

bool VectorTileGeometryBuilder::PolylinesBuilder::end_linestring()
{
    if (_current_linestring_size == 0) return true;
    HRZ_DEFER[this]()
    {
        _current_linestring_size = 0;
    };

    auto first_point_index_opt = _parent->_points.size();
    if (!first_point_index_opt.has_value()) return false;

    const auto first_point_index =
        (uint32_t)(first_point_index_opt.value() - _current_linestring_size);

    // The current feature is still empty, so we can simply store the linestring
    // (which has to be the only one for now) as a simple polyline feature.
    if (_feature->geometry_part_count == 0)
    {
        assert(!_part);

        _feature->first_geometry_part = first_point_index;
        _feature->geometry_part_count = _current_linestring_size;
        _feature->flags =
            FeatureFlags::make_simple_feature(hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY);
        return true;
    }

    // Otherwise, we'll need to use a geometry part.

    if (!_part)
    {
        // If the current feature is a simple polyline, we can transform it into a multi-polyline,
        // and reuse that new part.
        if (!_feature->flags.is_collection
            && _feature->flags.primitive_geometry_type()
                == hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY)
        {
            auto part_index = _parent->move_simple_feature_to_geometry_part(_feature);
            if (!part_index) return false;

            _part = _parent->_geometry_parts.at(part_index.value());
        }
        // Otherwise we add a new part to the feature.
        else
        {
            _part = _parent->add_geometry_part_to_feature(
                _feature, hrz_proto::VectorGeometryType::POLYLINE_GEOMETRY);
            _part->first_point = first_point_index;
            if (!_part) return false;
        }
    }
    assert(_part);

    _parent->add_linestring_to_geometry_part(_part, _current_linestring_size);

    return true;
}

bool VectorTileGeometryBuilder::PolylinesBuilder::finish()
{
    return end_linestring();
}

VectorTileGeometryBuilder::PolylinesBuilder::~PolylinesBuilder()
{
    assert(_current_linestring_size == 0);
}

void VectorTileGeometryBuilder::PolygonsBuilder::add_point(const lm::dvec3& point)
{
    _parent->_points.push_back(point);
    _current_ring_size += 1;
}

bool VectorTileGeometryBuilder::PolygonsBuilder::end_ring()
{
    if (_current_ring_size == 0) return true;
    HRZ_DEFER[this]()
    {
        _current_ring_size = 0;
    };

    auto first_point_index_opt = _parent->_points.size();
    if (!first_point_index_opt.has_value()) return false;

    const auto first_point_index = (uint32_t)(first_point_index_opt.value() - _current_ring_size);

    // The current feature is still empty, and this is the first ring,
    // so this is a simple polygon that can be stored without a geometry part.
    if (_feature->geometry_part_count == 0)
    {
        assert(_current_ring_count == 0 && !_part);

        _feature->first_geometry_part = first_point_index;
        _feature->geometry_part_count = _current_ring_size;
        _feature->flags =
            FeatureFlags::make_simple_feature(hrz_proto::VectorGeometryType::POLYGON_GEOMETRY);

        _current_ring_count = 1;

        return true;
    }

    // There is currently already a single ring built as a simple polygon,
    // so we need to convert it into a polygon with holes.
    // Note that we can't distinguish between a fully built simple polygon (with end_polygon()
    // called) and a simple polygon that is still being built (with only end_ring() called), so we
    // have _current_polygon_count to track the number of polygons that have been fully built. It's
    // a bit redundant when we have a geometry part, but oh well...
    if (!_feature->flags.is_collection && _current_ring_count == 1 && _current_polygon_count == 0)
    {
        assert(
            _feature->flags.primitive_geometry_type()
            == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY);
        assert(_feature->geometry_part_count > 0);

        auto part_index = _parent->move_simple_feature_to_geometry_part(_feature);
        if (!part_index) return false;

        _part = _parent->_geometry_parts.at(part_index.value());
    }
    else if (!_part)
    {
        _part = _parent->add_geometry_part_to_feature(
            _feature, hrz_proto::VectorGeometryType::POLYGON_GEOMETRY);
        if (!_part) return false;

        _part->first_point = first_point_index;
    }

    assert(_feature->flags.is_collection);
    assert(_part->type == hrz_proto::VectorGeometryType::POLYGON_GEOMETRY);

    _parent->add_linestring_to_geometry_part(_part, _current_ring_size);
    _current_ring_count += 1;

    return true;
}

bool VectorTileGeometryBuilder::PolygonsBuilder::end_exterior_ring()
{
    if (_current_ring_size == 0) return true;

    const uint32_t old_current_ring_size = _current_ring_size;

    // Ending an exterior ring is equivalent to ending the previous polygon, and starting a new one.
    // However we might have already started appending new points for this polygon.
    // So we pretend we have not done so, end the last polygon, then restore the current ring size
    // and end the current ring.

    _current_ring_size = 0;
    if (!end_polygon())
    {
        _current_ring_size = old_current_ring_size;
        return false;
    }

    _current_ring_size = old_current_ring_size;
    return end_ring();
}

bool VectorTileGeometryBuilder::PolygonsBuilder::end_polygon()
{
    if (!end_ring()) return false;
    if (_current_ring_count == 0) return true;

    HRZ_DEFER[this]()
    {
        _current_ring_count = 0;
    };

    if (!_part)
    {
        assert(_current_ring_count == 1 && _current_polygon_count == 0);
        _current_polygon_count = 1;
        return true;
    }

    _part = nullptr;
    _current_polygon_count += 1;
    return true;
}

bool VectorTileGeometryBuilder::PolygonsBuilder::finish()
{
    return end_polygon();
}

std::span<const lm::dvec3> VectorTileGeometryBuilder::PolygonsBuilder::current_ring_points() const
{
    if (_current_ring_size == 0) return {};

    auto points_data_opt = _parent->_points.data();
    if (!points_data_opt.has_value()) return {};

    const auto first_point_index = (uint32_t)(points_data_opt->size() - _current_ring_size);
    return points_data_opt->subspan(first_point_index, _current_ring_size);
}

VectorTileGeometryBuilder::PolygonsBuilder::~PolygonsBuilder()
{
    assert(_current_ring_size == 0);
}

} // namespace hrz::vector_data
