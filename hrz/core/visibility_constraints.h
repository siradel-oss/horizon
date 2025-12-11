#pragma once

#include "hrz/protocol/visibility_constraints.pb.h"

#include <lin_maths.h>

namespace hrz
{
struct RenderViewInfo;

namespace layers
{
bool are_visibility_constraints_satisfied(
    const lm::dvec3& camera_ecef_pos,
    const hrz_proto::LayerVisibilityConstraintList&);

struct MultiviewVisibilityConstraints
{
    // It's important to know what views are active to properly compare the
    // "satisfied_in" bitsets.  Otherwise there is no difference between an
    // inactive view and a view in which the constraints are not satisfied.
    // Thus comparing this entire struct should be enough to distinguish those
    // cases.
    uint32_t active_views = 0;
    uint32_t satisfied_in = 0;

    constexpr bool operator==(const MultiviewVisibilityConstraints& other) const = default;
};

MultiviewVisibilityConstraints are_visibility_constraints_satisfied(
    std::span<const RenderViewInfo> views_info,
    const hrz_proto::LayerVisibilityConstraintList&);

} // namespace layers
} // namespace hrz
