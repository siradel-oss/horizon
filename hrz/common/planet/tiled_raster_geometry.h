#pragma once

#include "hrz/common/proto_maths.h"
#include "hrz/protocol/geo/srs.pb.h"
#include "hrz/protocol/raster/geometry.pb.h"
#include "hrz/protocol/raster/tiling_scheme.pb.h"

#include <lin_maths.h>

namespace hrz::planet
{

// This mirrors the hrz_proto::RasterGeometry message, but adds the tiling scheme.
struct TiledRasterGeometry
{
    static TiledRasterGeometry from_geometry_and_tiling_scheme(
        const hrz_proto::RasterGeometry& geometry,
        const hrz_proto::TilingSchemeParams& tiling_scheme)
    {
        TiledRasterGeometry result;
        result.projection = geometry.projection();
        result.projection_bounds = to_lm(geometry.projection_bounds());
        result.bounds = to_lm(geometry.bounds());
        result.tiling_scheme = tiling_scheme;
        return result;
    }

    hrz_proto::SpatialReferenceSystem projection;
    lm::dbbox2 projection_bounds;
    lm::dbbox2 bounds;
    hrz_proto::TilingSchemeParams tiling_scheme;
};

} // namespace hrz::planet
