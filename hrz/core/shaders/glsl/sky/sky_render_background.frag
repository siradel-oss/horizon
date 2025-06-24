#include "common/logz.glsl"
#include "common/colors.glsl"
#include "common/ubo_frame.glsl"
#include "sky/sky.glsl"
#include "sky/sky_params_ubo.glsl"

layout(location = 0) out vec4 o_color;

in vec3 v_coords;

#ifdef USE_SKY_VIEW
uniform sampler2D u_sky_view;
uniform sampler2D u_transmittance_lut;
#endif

float ease_in_out_quad(float x) {
    return x < 0.5 ? 2.0 * x * x : 1.0 - ((-2.0 * x + 2.0) * (-2.0 * x + 2.0)) * 0.5;
}

void main()
{
    vec3 coords = normalize(v_coords);
    vec2 polar_uv = encode_sky_view_uv(coords, hrz_sky.horizon_horizon_angle);

#ifdef USE_SKY_VIEW
    vec4 in_scattered = texture(u_sky_view, polar_uv);
    vec3 color = in_scattered.rgb;

    vec3 sun_dir = vec3(
        cos(hrz_sky.sun_horizon_angle),
        0,
        sin(hrz_sky.sun_horizon_angle)
    );

    // This is about twice as large as the actual sun.
    // However it didn't look right with the real values, so here's some
    // artistic license for you.
    const float min_sun_cos_angle = 0.99995736863312987036;
    const float max_sun_cos_angle = 0.99998736863312987036;
    float sun_dot = smoothstep(min_sun_cos_angle, max_sun_cos_angle, dot(sun_dir, coords));
    if (sun_dot > 0.0)
    {
        // For simplicity, we apply the same transmittance to the entire sun.
        // This it simpler, faster, and is OK because the sun is really small in the sky.
        vec3 trans_sun = sample_transmittance(hrz_sky.altitude + float(EARTH_RADIUS), hrz_sky.sun_horizon_angle, u_transmittance_lut);
        color += sun_dot * trans_sun;
    }

    color *= float(HRZ_S_SKY_EXPOSURE);

    vec3 sky_color = vec3(0.0);
#else
    vec4 in_scattered = vec4(0.0, 0.0, 0.0, 1.0);
    vec3 color = vec3(0.0);
    vec3 sky_color_oklab;

    float angle = asin(coords.z);
    if (angle < hrz_sky.color_transition_horizon_start_angle)
    {
        sky_color_oklab = hrz_sky.atmosphere_color_oklab;
    }
    else
    {
        float color_transition = ease_in_out_quad(clamp((angle - hrz_sky.color_transition_horizon_start_angle)
            / (hrz_sky.color_transition_horizon_end_angle - hrz_sky.color_transition_horizon_start_angle), 0.0, 1.0));
        sky_color_oklab = mix(hrz_sky.atmosphere_color_oklab, hrz_sky.space_color_oklab, color_transition);
    }

    vec3 sky_color = oklab_to_linear(sky_color_oklab);
#endif

    vec3 terrain_color = hrz_sky.underground_color_linear;
    vec3 background_color = mix(terrain_color, sky_color, smoothstep(0.45, 0.47, polar_uv.y));

    o_color = vec4(
        linear_to_srgb(color + background_color * in_scattered.a),
        1.0);
}
