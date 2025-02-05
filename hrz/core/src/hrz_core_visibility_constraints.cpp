#include "hrz_core_visibility_constraints.h"

#include "hrz_core_render.h"

#include <hrz_common_geo.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_proto_maths.h>
#include <hrz_fnd_log.h>

namespace hrz::layers
{
bool are_visibility_constraints_satisfied(
    const lm::dvec3& camera_ecef_pos,
    const hrz_proto::LayerVisibilityConstraintList& visibility_constraints)
{
    if (visibility_constraints.constraints().empty()) return true;

    hrz::GeoPosition3 geo = hrz::ecef_to_geo3(camera_ecef_pos);

    for (const auto& constraint : visibility_constraints.constraints())
    {
        if (constraint.type() == hrz_proto::LayerVisibilityConstraintType::BOUNDS)
        {
            GeoBounds bounds = hrz::from_proto(constraint.bounds().bounds());

            bool is_inside_bounds = hrz::contains(bounds, geo.latlon());

            if (constraint.bounds().relative_position()
                == hrz_proto::RelativeBoundsPositionQualifier::OUTSIDE)
            {
                is_inside_bounds = !is_inside_bounds;
            }

            if (!is_inside_bounds)
            {
                return false;
            }
        }
        else if (constraint.type() == hrz_proto::LayerVisibilityConstraintType::ALTITUDE)
        {
            bool is_below_threshold = geo.alt < constraint.altitude().altitude();

            if (constraint.altitude().relative_position()
                == hrz_proto::RelativePositionQualifier::ABOVE)
            {
                is_below_threshold = !is_below_threshold;
            }

            if (!is_below_threshold)
            {
                return false;
            }
        }
        else
        {
            HRZ_LOG_ERROR(
                "Unknown constraint type \"{}\".",
                hrz_proto::LayerVisibilityConstraintType_Name(constraint.type()));
        }
    }

    return true;
}

MultiviewVisibilityConstraints are_visibility_constraints_satisfied(
    gsl::span<const RenderViewInfo> views_info,
    const hrz_proto::LayerVisibilityConstraintList& constraints)
{
    MultiviewVisibilityConstraints result;

    for (const auto& view_info : views_info)
    {
        uint32_t bit = (1 << (int)view_info.view);
        result.active_views |= bit;

        if (are_visibility_constraints_satisfied(view_info.cam_view_info.cam.pos, constraints))
        {
            result.satisfied_in |= bit;
        }
    }

    return result;
}

} // namespace hrz::layers
