// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "sky/sky_params_ubo.glsl"
#include "sky/sky_precompute.glsl"

layout(location = 0) out highp vec4 o_transmittance_lut;
layout(location = 1) out highp vec4 o_sun_color;

// We always compute the transmittance for a ray from the observer all the way
// to the limit of the atmosphere, or the ground. We do this so that the
// transmittance is only function two parameters (altitude and horizontal angle)
// so it can be pre-computed and stored in a 2D map.
//
// Since transmittance if multiplicative, we can retrieve the transmittance
// for any point inside the atmosphere. If we have S a point on the segment
// PA that goes from the observer to the limit of the atmosphere (or the ground)
// we have T(PA) = T(PS) * T(SA), so we can compute T(PS) = T(PA) / T(SA).
//
// Note that to compute T(SA) we'll need to compute the altitude and the
// horizontal angle at point S, but this can be done using the Ray utility
// described above.

void main()
{
    // This is the transmittance LUT used for atmosphere precomputing.
    {
        vec2 uv = gl_FragCoord.xy / float(HRZ_S_SKY_TRANSMITTANCE_LUT_SIZE);
        uv.y = 1.0 - uv.y;

        float horizon_angle;
        float altitude;
        decode_transmittance_uv(uv, altitude, horizon_angle);

        vec3 optical_depth = compute_optical_depth(
            altitude, horizon_angle,
            vec2(rayleigh_reference_altitude_inv, mie_reference_altitude_inv));

        vec3 transmittance = compute_transmittance(hrz_sky.cloudiness, optical_depth);

        o_transmittance_lut = vec4(transmittance, 1.0);
    }

    // This is the sun color LUT, directly derived from transmittance, used
    // for lighting.
    {
        vec2 uv = gl_FragCoord.xy / float(HRZ_S_SKY_TRANSMITTANCE_LUT_SIZE);

        float horizon_angle;
        float altitude;
        decode_sun_color_uv(uv, altitude, horizon_angle);

        bool intersect_earth = test_intersect_sphere(EARTH_RADIUS - 1.0, altitude, horizon_angle);
        if (!intersect_earth)
        {
            vec3 optical_depth = compute_optical_depth(
                altitude, horizon_angle,
                vec2(rayleigh_reference_altitude_inv, mie_reference_altitude_inv));

            vec3 transmittance = compute_transmittance(hrz_sky.cloudiness, optical_depth);

            o_sun_color = vec4(transmittance, 1.0);
        }
        else
        {
            o_sun_color = vec4(0.0, 0.0, 0.0, 1.0);
        }
    }
}
