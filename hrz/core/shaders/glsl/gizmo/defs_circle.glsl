#pragma once

#define CIRCLE_GIZMO_RADIUS_MULTIPLIER 3.0

layout(std140) uniform Gizmo
{
    vec4 circle_inner_color;
    vec4 circle_outline_color;
    vec3 offset;
    float circle_radius;
    float circle_outline_size;
} hrz_gizmo;
