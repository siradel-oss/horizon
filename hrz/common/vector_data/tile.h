// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/tile_coords.h"
#include "hrz/common/vector_data/feature_ids.h"
#include "hrz/common/vector_data/packed_attribute_values.h"
#include "hrz/common/vector_data/tile_geometry.h"

namespace hrz::vector_data
{

struct DecodedVectorTile
{
    hrz::TileCoords coords;

    // There can be multiple features with the same id.
    // This is the case for multipoints, multipolylines and multipolygons.
    hrz::vector_data::FeatureIds feature_ids;

    hrz::vector_data::VectorTileGeometry geometry;
    std::vector<hrz::vector_data::AttributeValues> attributes;
};

} // namespace hrz::vector_data
