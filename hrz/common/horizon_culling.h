// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/geo.h"

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

    constexpr bool operator ==(const HorizonCuller& other) const = default;
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
