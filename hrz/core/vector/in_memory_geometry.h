// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/fnd/function_ref.h"
#include "hrz/protocol/vector/feature.pb.h"

#include <lin_maths.h>

#include <span>
#include <vector>

namespace hrz::vector_data::in_memory
{

// Geometry arrays in the same enoding as tile geometries, but
// in simple std::vectors, not blobs.
struct GeometryArrays
{
    std::vector<VectorTileGeometry::GeometryPart> parts;
    std::vector<lm::dvec3> points;
    std::vector<uint32_t> sizes;
};

// A feature without geometry: an empty collection with a NaN anchor.
VectorTileGeometry::Feature make_empty_feature();

// Geometry of a single in-memory feature, in the tile geometry encoding, with indices
// that refer to its own arrays.
struct FeatureGeometry
{
    VectorTileGeometry::Feature encoded = make_empty_feature();
    GeometryArrays arrays;
    lm::dbbox3 bbox = lm::dbbox3::invalid();
};

// Decodes all the geometries of a feature, projects them, and encodes them, computing the
// feature's anchor and bounding box.
// Invalid geometries are skipped with a warning. The resulting feature may have no geometry.
// project is called once, with all the points of the feature.
FeatureGeometry decode_feature_geometry(
    const hrz_proto::VectorFeature& feature,
    hrz::function_ref<void(std::span<lm::dvec3>)> project);

// Appends a feature's geometry to other arrays, and returns the feature with indices
// that refer to these arrays.
VectorTileGeometry::Feature append_feature_geometry(
    const FeatureGeometry& geometry,
    GeometryArrays* dst);

} // namespace hrz::vector_data::in_memory
