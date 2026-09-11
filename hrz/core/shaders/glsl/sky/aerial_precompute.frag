// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "common/ubo_frame.glsl"
#include "sky/sky_params_ubo.glsl"
#include "sky/sky_precompute.glsl"

layout(location = 0) out vec4 o_color;

uniform sampler2D u_transmittance_lut;

void main()
{
    // We can't render into a 3D texture so instead the Z slices are
    // on the y axis.
    vec3 clip;
    clip.x = gl_FragCoord.x / float(HRZ_S_SKY_AERIAL_WIDTH);
    clip.y = modf(gl_FragCoord.y / float(HRZ_S_SKY_AERIAL_HEIGHT), clip.z);

    // In OpenGL, depth in negative
    float depth = -aerial_depth_slice(clip.z / float(HRZ_S_SKY_AERIAL_DEPTH - 1));

    // First we compute the direction of the view direction in camera space.
    clip = (clip * 2.0) - vec3(1.0);
    vec4 world_pt = hrz_frame.proj_inv_matrix * vec4(clip, 1.0);
    world_pt.xyz = normalize(world_pt.xyz);
    world_pt.w = 1.0;

    // The depth cannot be reconstructed directly via the inverse projection
    // matrix because it's too large, so we reconstruct it manually by scaling the
    // direction vector along the z axis.
    world_pt.xyz = world_pt.xyz * depth / world_pt.z;

    // Finally we can go to world space, and then to the tangential frame
    // space, in which we do all the sky stuff.
    world_pt = hrz_frame.view_cc_inv_matrix * world_pt;
    vec4 tangent_pt = hrz_sky.sky_box_inv_rot * world_pt;
    float max_distance = length(tangent_pt);
    vec3 tangent_dir = normalize(tangent_pt.xyz);
    float azimuth = atan(tangent_dir.y, tangent_dir.x);

    float altitude = hrz_sky.altitude + EARTH_RADIUS;
    float horizon_angle = asin(tangent_dir.z);

    vec4 in_scattered = vec4(0.0);
    float from, to;
    if (atmosphere_intersection(altitude, horizon_angle, from, to))
    {
        from = min(max_distance, from);
        to = max_distance;

        if (from < to)
        {
            in_scattered = in_scattering(
                altitude, horizon_angle, azimuth, hrz_sky.sun_horizon_angle,
                from, to,
                hrz_sky.cloudiness, u_transmittance_lut);
        }
    }

    o_color = in_scattered;
}
