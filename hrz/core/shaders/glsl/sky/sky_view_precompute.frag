// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "sky/sky_params_ubo.glsl"
#include "sky/sky_precompute.glsl"

layout(location = 0) out vec4 o_color_camera;
layout(location = 1) out vec4 o_color_sea_level;

uniform sampler2D u_transmittance_lut;

vec4 in_scattering_sky_view(
    float altitude,
    float horizon_angle,
    float azimuth,
    float sun_horizon_angle)
{
    float from, to;
    if (!atmosphere_intersection(altitude, horizon_angle, from, to))
    {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    return in_scattering(altitude, horizon_angle, azimuth, sun_horizon_angle,
        from, to, hrz_sky.cloudiness, u_transmittance_lut);
}

vec4 compute_at_camera(vec2 uv)
{
    float horizon_angle;
    float azimuth;
    decode_sky_view_uv(uv, hrz_sky.horizon_horizon_angle, azimuth, horizon_angle);

    float altitude = hrz_sky.altitude + EARTH_RADIUS;

    vec4 in_scattered = in_scattering_sky_view(altitude, horizon_angle, azimuth, hrz_sky.sun_horizon_angle);
    return in_scattered;
}

vec4 compute_at_sea_level(vec2 uv)
{
    const float altitude = 100.0 + float(EARTH_RADIUS);
    const float horizon_horizon_angle = -acos(float(EARTH_RADIUS) / altitude);

    float horizon_angle = uv.y;
    float azimuth = uv.x;
    decode_sky_view_uv(uv, horizon_horizon_angle, azimuth, horizon_angle);

    vec4 in_scattered = in_scattering_sky_view(altitude, horizon_angle, azimuth, hrz_sky.sun_horizon_angle);
    return in_scattered;
}

void main()
{
    vec2 uv = vec2(gl_FragCoord) / float(HRZ_S_SKY_VIEW_SIZE);

    o_color_camera = compute_at_camera(uv);
    o_color_sea_level = compute_at_sea_level(uv);
}
