#pragma once

#include "hrz_common_geo.h"

#include <lin_maths.h>

namespace hrz
{
class HorizonCuller
{
    lm::dvec3 _eye_pos;
    double _eye_pos_length = 0;
    double _horizon_distance_squared = 0;
    double _horizon_plane_distance = 0;

public:
    HorizonCuller() = default;

    explicit HorizonCuller(const lm::dvec3& eye_pos);

    bool is_occluded(lm::dvec3 position) const;

    bool operator==(const HorizonCuller& other) const
    {
        return _eye_pos == other._eye_pos && _eye_pos_length == other._eye_pos_length
            && _horizon_distance_squared == other._horizon_distance_squared
            && _horizon_plane_distance == other._horizon_plane_distance;
    }

    bool operator!=(const HorizonCuller& other) const { return !(*this == other); }
};

namespace horizon_culling
{
// Return a point whose occlusion by the horizon implies the occlusion of the whole volume.
// Such a point does not always exist, in which case it should be considered that the volume
// can never be fully occluded by the horizon.
std::optional<lm::dvec3> compute_occlusion_point(const hrz::GeoVolumeBounds& bounds);
std::optional<lm::dvec3> compute_occlusion_point(const hrz::OrientedBBox3<double>& box);
std::optional<lm::dvec3> compute_occlusion_point(const hrz::BSphere<double>& sphere);

} // namespace horizon_culling
} // namespace hrz
