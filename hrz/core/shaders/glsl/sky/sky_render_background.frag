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
    float sky_transition = smoothstep(0.45, 0.47, polar_uv.y);

#ifdef USE_SKY_VIEW
    vec4 in_scattered = texture(u_sky_view, polar_uv);
    vec3 sky_color = in_scattered.rgb;

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
        sky_color += sun_dot * trans_sun;
    }

    sky_color *= float(HRZ_S_SKY_EXPOSURE);

    vec4 underground_color = hrz_sky.underground_color;
    underground_color *= (1.0 - sky_transition);

    float sky_color_alpha = mix(1.0 - in_scattered.a, 1.0, sky_transition);

    o_color = vec4(sky_color, sky_color_alpha) + underground_color * (1.0 - sky_color_alpha);

    o_color = linear_to_srgb(o_color);
    // Use sRGB's gamma on the alpha channel.
    o_color.a = pow(o_color.a, 1.0 / 2.2);
#else
    vec4 sky_color = vec4(0.0);

    float angle = asin(coords.z);
    if (angle < hrz_sky.color_transition_horizon_start_angle)
    {
        sky_color = hrz_sky.atmosphere_color;
    }
    else
    {
        float color_transition = ease_in_out_quad(clamp((angle - hrz_sky.color_transition_horizon_start_angle)
            / (hrz_sky.color_transition_horizon_end_angle - hrz_sky.color_transition_horizon_start_angle), 0.0, 1.0));
        sky_color = mix(hrz_sky.atmosphere_color, hrz_sky.space_color, color_transition);
    }

    o_color = mix(hrz_sky.underground_color, sky_color, sky_transition);

    o_color.rgb /= o_color.a;
    o_color = oklab_to_linear(o_color);
    o_color.rgb *= o_color.a;

    o_color = linear_to_srgb(o_color);
    // Use sRGB's gamma on the alpha channel.
    o_color.a = pow(o_color.a, 1.0 / 2.2);
#endif
}
