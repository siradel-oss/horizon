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

// This checks whether the current view direction would intersect the planet surface.
// Note that when the camera is below some minimum elevation, we clamp in just above the surface.
// This is the same as hrz::planet_intersection in hrz/common/geo.h, but without the
// coordinate computation.
// Note that we also pretend that the planet is slightly smaller than it actually is, to avoid
// showing too much of the underground color at the horizon when we are in areas below
// sea level.
bool intersects_planet()
{
    const float min_elevation = 100.0;

    const vec3 inv_wgs84_ellipsoid = vec3(1.0, 1.0, 1.0 / HRZ_S_WGS84_AXES_LENGTH_RATIO);

    vec2 frag_coord = gl_FragCoord.xy / vec2(hrz_frame.viewport_size) * 2.0 - vec2(1.0);
    vec3 dir = (hrz_frame.view_cc_inv_matrix * hrz_frame.proj_inv_matrix * vec4(frag_coord, 0.0, 1.0)).xyz;
    vec3 dir_sphere = dir * inv_wgs84_ellipsoid;

    vec3 pos_sphere = hrz_frame.view_pos_low.xyz * inv_wgs84_ellipsoid
        + hrz_frame.view_pos_high.xyz * inv_wgs84_ellipsoid;

    if (hrz_frame.view_elevation < min_elevation)
    {
        pos_sphere = normalize(pos_sphere) * (HRZ_S_EARTH_RADIUS + min_elevation);
    }

    const float intersection_earth_radius = HRZ_S_EARTH_RADIUS - min_elevation;
    float a = dot(dir_sphere, dir_sphere);
    float b = 2.0 * dot(dir_sphere, pos_sphere);
    float c = dot(pos_sphere, pos_sphere) - intersection_earth_radius * intersection_earth_radius;
    float d = b * b - 4.0 * a * c;

    if (d >= 0.0)
    {
        float sqrt_delta = sqrt(d);
        float t0 = (-b - sqrt_delta) / (2.0 * a);
        float t1 = (-b + sqrt_delta) / (2.0 * a);
        float t = min(t0, t1);
        if (min(t0, t1) > 0.0)
        {
            return true;
        }
        else if (max(t0, t1) > 0.0)
        {
            return true;
        }
    }

    return false;
}

void main()
{
    vec3 coords = normalize(v_coords);
    vec2 polar_uv = encode_sky_view_uv(coords, hrz_sky.horizon_horizon_angle);
    bool show_underground = intersects_planet();

    if (!show_underground)
    {
        // When we're displaying the sky, we extend the sky horizon down
        // to the geometry horizon to avoid a black gap.
        // Otherwise the sample the sky view below the horizon to sample
        // the transmittance and in-scattered light for the underground color.
        polar_uv.y = max(0.51, polar_uv.y);
    }

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

    vec4 underground_color = vec4(0.0);
    float sky_color_alpha = 1.0;

    if (show_underground)
    {
        underground_color = hrz_sky.underground_color;
        sky_color_alpha = 1.0 - in_scattered.a;
    }

    o_color = vec4(sky_color, sky_color_alpha) + underground_color * (1.0 - sky_color_alpha);
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

    if (show_underground)
    {
        o_color = hrz_sky.underground_color;
    }
    else
    {
        o_color = sky_color;
    }

    if (hrz_sky.oklab_gradient)
    {
        // Oklab interpolation does not support premultiplied alpha, but
        // we should only enter this block when all colours are opaque.
        o_color = oklab_to_linear(o_color);
    }
#endif
}
