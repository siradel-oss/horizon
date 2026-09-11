// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/protocol/vector/geometry_type.pb.h"

#include <lin_maths.h>

#include <cstdint>

namespace hrz::vector_data
{

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

} // namespace hrz::vector_data
