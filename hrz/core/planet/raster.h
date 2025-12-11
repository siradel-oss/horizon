#pragma once

#include "hrz/core/visibility_constraints.h"
#include "hrz/protocol/layer/handle.pb.h"
#include "hrz/protocol/raster/blending.pb.h"
#include "hrz/protocol/raster/group.pb.h"
#include "hrz/protocol/raster/sampling.pb.h"
#include "hrz/protocol/visibility_constraints.pb.h"

#include <lin_maths.h>

#include <memory>

namespace hrz::planet
{
struct RasterProvider;

struct Raster
{
    // ID unique to a raster layer.
    uint64_t id;

    // This is a random ID that is unique to a raster instance, not a layer. It
    // can't be used for lookup but is useful for computing the rasters hash
    // used to detect changes in merge groups. Unlike the raster ID, this
    // changes when a raster is invalidated (for instance when a provider
    // changes).
    uint64_t unique_id;

    uint32_t slot;
    uint32_t scene_views;
    hrz_proto::RasterGroup raster_group;
    uint32_t merge_groups_bitset;
    bool is_visible;

    hrz_proto::LayerType type;
    std::unique_ptr<RasterProvider> provider;
    int8_t loading_priority;

    // In Web Mercator
    lm::dbbox2 display_bounds;
    hrz_proto::RasterSampling sampling;
    hrz_proto::RasterBlending blending;

    hrz_proto::LayerVisibilityConstraintList visibility_constraints;
    layers::MultiviewVisibilityConstraints visibility_constraints_result;
};

} // namespace hrz::planet
