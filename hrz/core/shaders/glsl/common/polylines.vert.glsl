// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

// Should match "HrzProtocol.DashSizeUnit" enum variants
#define DASH_SIZE_UNIT_METERS 0u
#define DASH_SIZE_UNIT_PIXELS 1u
#define DASH_SIZE_UNIT_RELATIVE 2u

float project_point_to_plane_along_direction_distance(vec3 vertex, vec3 plane_point, vec3 plane_normal, vec3 direction)
{
    vec3 to_vertex = vertex - plane_point;

    // This computes the offset to apply to the vertex to align it with a plane
    // defined by a point and a normal.
    //
    // All computation are done with the plane point at the origin. We want to
    // offset the vertex P along the direction d by the amount x, so P' = P + x.d
    //
    // We also want P' to lie in the plane, which has normal n, and goes through
    // the origin since that's how we defined our space. This plane's equation
    // is p.n = 0, for any p.
    //
    // By combining both expressions we have  n.P + x.(n.d) = 0.
    // We solve for x, and get the expression below.
    return -dot(plane_normal, to_vertex) / dot(plane_normal, direction);
}

vec3 project_point_to_plane_along_direction(vec3 vertex, vec3 plane_point, vec3 plane_normal, vec3 direction)
{
    return vertex + direction * project_point_to_plane_along_direction_distance(vertex, plane_point, plane_normal, direction);
}
