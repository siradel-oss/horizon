#include "common/logz.glsl"
#include "common/colors.glsl"
#include "common/ubo_frame.glsl"
#include "common/camera.glsl"
#include "sky/sky.glsl"
#include "sky/sky_params_ubo.glsl"

layout(location = 0) out vec4 o_color;

in vec3 v_coords;

#ifdef USE_SKY_VIEW
uniform sampler2D u_sky_view;
uniform sampler2D u_aerial_lut;
#endif

uniform sampler2D u_color;
uniform sampler2D u_depth;

#ifdef USE_FOG
vec4 compute_fog(FogParams fog, float view_distance, float distance_to_altitude, float horizon_angle, bool in_sky)
{
    // For a set position in space, we define the fog opacity f, which describes the amount of light
    // to be replaced by the fog color at this location. It decreases exponentially with altitude:
    //     f = fog.density * exp(fog.falloff_factor * (altitude - fog.falloff_start))
    //
    // We express opacity in function of the transmittance T which corresponds to a probability
    // for light to progress unhindered by fog:
    //     f = 1 - T
    //
    // To apply the correct amount of fog for each fragment, we integrate the transmittance along
    // the view direction using a product integral:
    //     ⫪(T, start, end) = ⫪(1 - f, start, end)
    //
    // Luckily enough this is equal to:
    //     exp(∫(-f, start, end))
    // (https://en.wikipedia.org/wiki/Product_integral#Type_I:_Volterra_integral)
    //
    // From that integrated transmittance, we find the final fog opacity:
    //     1 - exp(∫(-f, start, end))
    //
    // To compute the fog opacity at any given position in space, we approximate the altitude of
    // a point based on the camera altitude and the distance from the point to the camera
    // (using the angle between the direction to that point and the ground normal):
    //     altitude = hrz_sky.altitude + distance_to_altitude * view_distance
    //
    // This approximation degrades as the camera moves away from the Earth, because it
    // assumes altitude to be relative to a flat plane at the camera position on the surface of the
    // Earth sphere; but it allows us to easily integrate the exponential fog opacity along the view
    // direction.

    float from = fog.start_distance;
    float to = view_distance;

    if (in_sky)
    {
        if (!fog.apply_to_sky)
        {
            return vec4(0.0);
        }

        // When integrating along a direction that does not intersect the Earth, we restrict
        // the integration bounds to fit the interesection between the view direction and the
        // "fog sphere", which is the fog layer that covers the Earth. We consider it to be an
        // extension of the Earth sphere by about the height of the fog. This helps to prevent
        // issues tied to the divergence between the estimation of the altitude we compute using
        // the view distance, which does not account for the curvature of the Earth, and the
        // real altitude.
        float d0;
        float d1;

        // We use double the value of falloff_end because it doesn't actually reprensent the height
        // at which the fog ends, but rather the height at which there is almost no fog remaining.
        // Using falloff_end directly can result in discontinuities between the fog applied to
        // objects or the Earth, and the fog applied to the sky. Using the double of this value
        // avoids this issue, and still prevents high altitude artifacts due to the imprecision
        // of the altitude estimation, at least for "reasonable" fog heights...
        int hits = intersect_sphere(2.0 * fog.falloff_end + float(HRZ_S_EARTH_RADIUS), hrz_sky.altitude + float(HRZ_S_EARTH_RADIUS), horizon_angle, d0, d1);

        if (hits == 0)
        {
            return vec4(0.0);
        }
        else if (hits == 1)
        {
            to = d0;
        }
        else
        {
            from = d0;
            to = d1;
        }
    }

    // Compute the integral of the fog factor along the view distance
    float f = 0.0;
    if (distance_to_altitude == 0.0)
    {
        f = min(1.0, exp(fog.falloff_factor * (hrz_sky.altitude - fog.falloff_start)) * (to - from));
    }
    else if (distance_to_altitude > 0.0)
    {
        // Before this distance, we are under the falloff start, and the fog is value is 1
        float threshold_distance = (fog.falloff_start - hrz_sky.altitude) / distance_to_altitude;

        if (threshold_distance > to)
        {
            f = to - from;
        }
        else
        {
            if (threshold_distance > from)
            {
                f += threshold_distance - from;
                from = threshold_distance;
            }
            f += 1.0 / (fog.falloff_factor * distance_to_altitude) * (exp(fog.falloff_factor * (hrz_sky.altitude - fog.falloff_start + distance_to_altitude * to)) - exp(fog.falloff_factor * (hrz_sky.altitude - fog.falloff_start + distance_to_altitude * from)));
        }
    }
    else
    {
        // After this distance, we are under falloff start, and the fog value is 1
        float threshold_distance = (fog.falloff_start - hrz_sky.altitude) / distance_to_altitude;

        if (threshold_distance < from)
        {
            f = to - from;
        }
        else
        {
            if (threshold_distance < to)
            {
                f += to - threshold_distance;
                to = threshold_distance;
            }
            f += 1.0 / (fog.falloff_factor * distance_to_altitude) * (exp(fog.falloff_factor * (hrz_sky.altitude - fog.falloff_start + distance_to_altitude * to)) - exp(fog.falloff_factor * (hrz_sky.altitude - fog.falloff_start + distance_to_altitude * from)));
        }
    }

    f = 0.001 * fog.density * f;
    f = 1.0 - exp(-f);
    f = clamp(f, 0.0, 1.0);

    return vec4(fog.color.rgb * fog.color.a * f, fog.color.a * f);
}
#endif

void main()
{
    vec2 bb_size = vec2(hrz_frame.viewport_size);
    vec2 uv = gl_FragCoord.xy / bb_size;
    vec4 color = texture(u_color, uv);
    float depth_raw = texture(u_depth, uv).r;

    bool in_sky = depth_raw >= 1.0;

    vec3 coords = normalize(v_coords);
    float depth = undo_log_depth(depth_raw);

#ifdef USE_FOG
    if (depth > hrz_sky.fog_min_depth && !(in_sky && !hrz_sky.fog[0].apply_to_sky && !hrz_sky.fog[1].apply_to_sky))
    {
        vec3 clip;
        clip.x = gl_FragCoord.x / float(hrz_frame.viewport_size.x);
        clip.y = gl_FragCoord.y / float(hrz_frame.viewport_size.y);
        // The forward direction is negative Z
        clip.z = -depth / float(HRZ_S_FAR);

        clip = (clip * 2.0) - vec3(1.0);
        vec4 view_pt = hrz_frame.proj_inv_matrix * vec4(clip, 1.0);
        view_pt.xyz /= view_pt.w;
        view_pt.w = 1.0;

        // The depth cannot be reconstructed directly via the inverse projection
        // matrix because it's too large, so we reconstruct it manually by scaling the
        // direction vector along the z axis.
        view_pt.xyz = view_pt.xyz * -depth / view_pt.z;
        float view_distance = length(view_pt.xyz);

        // The cosinus of the angle between the vector going from the camera to the fragment
        // position and the ground normal.
        // Multiplying it by a distance gives us an estimation of the difference in altitude
        // between the camera and a position along that vector.
        // (It is an estimation because it considers the altitude to be relative to a plane.)
        float distance_to_altitude = dot(view_pt.xyz / view_distance, hrz_sky.ground_normal_view);
        float horizon_angle = PI / 2.0 - acos(distance_to_altitude);

        const float camera_height_fade_start = 200000.0;
        const float camera_height_fade_end   = 2000000.0;
        float camera_height_factor = 1.0 - clamp(
            (hrz_sky.altitude - camera_height_fade_start) / (camera_height_fade_end - camera_height_fade_start),
            0.0, 1.0);

        if (hrz_sky.fog[0].enabled)
        {
            vec4 fog = compute_fog(hrz_sky.fog[0], view_distance, distance_to_altitude, horizon_angle, in_sky);
            fog *= camera_height_factor;
            color = fog + color * (1.0 - fog.a);
        }

        if (hrz_sky.fog[1].enabled)
        {
            vec4 fog = compute_fog(hrz_sky.fog[1], view_distance, distance_to_altitude, horizon_angle, in_sky);
            fog *= camera_height_factor;
            color = fog + color * (1.0 - fog.a);
        }
    }
#endif

#ifdef USE_SKY_VIEW
    if (!in_sky)
    {
        // For geometry "close" to the camera, we can use the aerial LUT for
        // aerial perspective, and when the geometry is really far we use the
        // sky view. In order to avoid discontinuities, we smoothly interpolate
        // between them.

        const float min_aerial_depth = float(HRZ_S_SKY_AERIAL_MAX_DEPTH) * 0.5;
        vec4 aerial_contrib = vec4(0.0);
        vec4 sky_view_contrib = vec4(0.0);

        float atmosphere_opacity = smoothstep(hrz_frame.atmosphere_fade_start,
            hrz_frame.atmosphere_fade_end, depth);

        {
            vec3 coords = normalize(v_coords);
            vec2 polar_uv = encode_sky_view_uv(coords, hrz_sky.horizon_horizon_angle);
            vec4 in_scattered = texture(u_sky_view, polar_uv);

            vec4 terrain_color = color * mix(1.0, in_scattered.a, atmosphere_opacity);
            vec4 scattered_color = vec4(in_scattered.rgb * float(HRZ_S_SKY_EXPOSURE) * atmosphere_opacity, atmosphere_opacity);
            sky_view_contrib = scattered_color + terrain_color;
        }

        if (hrz_sky.altitude <= STRAT_RADIUS - EARTH_RADIUS && depth < float(HRZ_S_SKY_AERIAL_MAX_DEPTH))
        {
            float slice = aerial_depth_to_slice(depth);
            float slice_0 = floor(slice);
            float slice_1 = min(slice_0 + 1.0, float(HRZ_S_SKY_AERIAL_DEPTH) - 1.0);
            float slice_fract = fract(slice);

            vec2 uv = (gl_FragCoord.xy - vec2(0.5)) / (bb_size - vec2(1.0))
                * vec2(float(HRZ_S_SKY_AERIAL_WIDTH) - 1.0, float(HRZ_S_SKY_AERIAL_HEIGHT) - 1.0)
                / vec2(float(HRZ_S_SKY_AERIAL_WIDTH), float(HRZ_S_SKY_AERIAL_HEIGHT))
                + 0.5 / vec2(float(HRZ_S_SKY_AERIAL_WIDTH), float(HRZ_S_SKY_AERIAL_HEIGHT));

            vec2 aerial_uv_0 = uv;
            aerial_uv_0.y = uv.y / float(HRZ_S_SKY_AERIAL_DEPTH) + slice_0 / float(HRZ_S_SKY_AERIAL_DEPTH);

            vec2 aerial_uv_1 = uv;
            aerial_uv_1.y = uv.y / float(HRZ_S_SKY_AERIAL_DEPTH) + slice_1 / float(HRZ_S_SKY_AERIAL_DEPTH);

            vec4 in_scattered_0 = texture(u_aerial_lut, aerial_uv_0);
            vec4 in_scattered_1 = texture(u_aerial_lut, aerial_uv_1);

            vec4 in_scattered = mix(in_scattered_0, in_scattered_1, slice_fract);

            vec4 terrain_color = color * mix(1.0, in_scattered.a, atmosphere_opacity);
            vec4 scattered_color = vec4(in_scattered.rgb * float(HRZ_S_SKY_EXPOSURE) * atmosphere_opacity, atmosphere_opacity);
            aerial_contrib = scattered_color + terrain_color;
        }

        float interpolation = 0.0;
        if (hrz_sky.altitude > STRAT_RADIUS - EARTH_RADIUS)
        {
            interpolation = 1.0;
        }
        else
        {
            interpolation = smoothstep(min_aerial_depth, float(HRZ_S_SKY_AERIAL_MAX_DEPTH), depth);
        }

        color = mix(aerial_contrib, sky_view_contrib, interpolation);
    }
#endif

    o_color = color;
}
