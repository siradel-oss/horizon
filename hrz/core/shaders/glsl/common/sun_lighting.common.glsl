#pragma once

#include "common/ubo_frame.glsl"
#include "sky/sky.glsl"

#if !defined(ATMOSPHERE_DISABLED)
uniform sampler2D hrz_sun_color_lut;
#endif // !defined(ATMOSPHERE_DISABLED)

vec3 compute_sun_lighting_ambient(vec3 normal, vec3 sun_dir, float altitude, vec3 normal_to_ground)
{
    vec4 env_normal = vec4(normal, 1);
    float r = dot(env_normal, hrz_frame.env_sh[0] * env_normal);
    float g = dot(env_normal, hrz_frame.env_sh[1] * env_normal);
    float b = dot(env_normal, hrz_frame.env_sh[2] * env_normal);

    // This x4 is a bit empirical. Technically I should have multiplied by
    // 4xPi to correctly normalize the spherical harmonics, but it looked
    // way too bright.
    //      -slerouzic, 2020-10-06
    vec3 ambient = vec3(r, g, b) * 4.0 * hrz_frame.ambient_strength;

    if ((hrz_frame.atmosphere_flags & DYNAMIC_AMBIENT_LIGHTING) != 0u)
    {
        // Since the ambient color is computed at sea level, when we see a part
        // of the Earth that is at night while hovering over a part that is at
        // day, the ambient lighting of the day is applied at night, because
        // it's the same everywhere. This is not good because it makes it too
        // bright. So what we do is use the angle between the normal to ground
        // and the sun direction so compute a factor telling us if we are on
        // the side of the Earth that is in front of the sun or not. Then we
        // use this to attenuate the ambient lighting where it's night. We only
        // apply this factor when we are really high because we do want
        // accurate ambient lighting for the night when we are in night parts
        // and are low enough that the ambient "leak" isn't an issue because we
        // can't see too far away.

        float day_factor = clamp(dot(sun_dir, normal_to_ground), 0.0, 1.0);
        const float strat_alt = float(HRZ_S_STRAT_RADIUS) - float(HRZ_S_EARTH_RADIUS);
        float alt_factor = smoothstep(strat_alt * 0.1, strat_alt * 10.0, hrz_frame.view_elevation);
        ambient *= mix(1.0, day_factor, alt_factor);
    }

    return ambient;
}

vec3 compute_sun_lighting_diffuse(vec3 normal, vec3 sun_dir, float altitude, vec3 normal_to_ground)
{
    vec3 sun_color = hrz_frame.sun_color;

#if !defined(ATMOSPHERE_DISABLED)
    if ((hrz_frame.atmosphere_flags & DYNAMIC_SUN_LIGHTING) != 0u)
    {
        vec2 sun_color_uv = encode_sun_color_uv(altitude, asin(dot(normal_to_ground, sun_dir)));
        sun_color = texture(hrz_sun_color_lut, sun_color_uv).rgb;
    }
#endif // !defined(ATMOSPHERE_DISABLED)

    // in [-1, 1]
    float exposure = dot(normal, sun_dir);

    // anything from -1 to 0
    float min_exposure = -hrz_frame.wrap_lighting;

    // in [min_exposure, 1];
    exposure = max(min_exposure, exposure);

    // in [0, 1]
    exposure = (exposure - min_exposure) / (1.0 - min_exposure);

    return exposure * sun_color * hrz_frame.sun_strength;
}
