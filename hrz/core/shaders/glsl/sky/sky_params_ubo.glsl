#pragma once

struct FogParams
{
    vec4 color;
    float density;
    float start_distance;
    float falloff_start;
    float falloff_end;
    float falloff_factor;
    bool enabled;
    bool apply_to_sky;
};

layout(std140) uniform SkyParams
{
    mat4 sky_box_pv;
    mat4 sky_box_inv_rot;
    float altitude; // Current altitude relative to the ground.
    float sun_horizon_angle;
    float cloudiness;
    float horizon_horizon_angle; // Horizontal angle of the horizon
    vec3 ground_normal_view;   // The planet normal vector at the camera position, in view space
    float fog_min_depth;
    FogParams fog[2];
    vec3 atmosphere_color_oklab;
    float color_transition_horizon_start_angle;
    vec3 space_color_oklab;
    float color_transition_horizon_end_angle;
    vec3 underground_color_linear;
} hrz_sky;
