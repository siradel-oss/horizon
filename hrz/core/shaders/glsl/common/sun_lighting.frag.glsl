#pragma once

#include "common/maths.glsl"
#include "common/sun_lighting.common.glsl"

#if !defined(SHADOWS_DISABLED)

in vec4 v_sun_shadows_pos_0;
in vec4 v_sun_shadows_pos_1;
in vec4 v_sun_shadows_pos_2;
uniform highp sampler2DShadow hrz_sun_shadow_map[HRZ_S_MAX_SUN_CASCADES];

bool is_in_shadow_frustum(vec3 pos)
{
    return pos.x > -1.0 && pos.x < 1.0 &&
        pos.y > -1.0 && pos.y < 1.0 &&
        pos.z > -1.0 && pos.z < 1.0;
}

float hash(vec2 p) { return fract(1e4 * sin(1745672.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 134473.0 + p.x)))); }

float sample_shadow(highp sampler2DShadow shadow_map, vec3 pos)
{
    float angle = hash(pos.xy) * PI * 2.0;
    float cos_angle = cos(angle);
    float sin_angle = sin(angle);
    mat2 rot = mat2(cos_angle, sin_angle, -sin_angle, cos_angle);

    const vec3 offsets_weights[9] = vec3[](
        vec3(-1.0, -1.0, 0.0625),
        vec3( 0.0, -1.0, 0.1250),
        vec3( 1.0, -1.0, 0.0625),
        vec3(-1.0,  0.0, 0.1250),
        vec3( 0.0,  0.0, 0.2500),
        vec3( 1.0,  0.0, 0.1250),
        vec3(-1.0,  1.0, 0.0625),
        vec3( 0.0,  1.0, 0.1250),
        vec3( 1.0,  1.0, 0.0325)
    );

    const float radius = 1.0 / float(HRZ_S_SUN_SHADOW_MAP_SIZE);
    const int NUM_SAMPLES = 9;
    const float bias = 0.0003;
    float sum = 0.0;
    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        vec2 offset = rot * offsets_weights[i].xy * radius;
        vec3 uv = pos * 0.5 + vec3(0.5) + vec3(offset, -bias);
        sum += texture(shadow_map, vec3(uv.xy, uv.z)) * offsets_weights[i].z;
    }
    return smoothstep(0.1, 0.9, sum);
}

float apply_shadow_maps(vec3 normal, vec3 sun_dir)
{
    uint shadow_map_cascade_count = hrz_frame.shadow_map_cascade_count;
    vec3 v_sun_shadows_pos_3 = vec3(v_sun_shadows_pos_0.w, v_sun_shadows_pos_1.w, v_sun_shadows_pos_2.w);

    if (is_in_shadow_frustum(v_sun_shadows_pos_0.xyz))
    {
        return sample_shadow(hrz_sun_shadow_map[0], v_sun_shadows_pos_0.xyz);
    }
    else if (shadow_map_cascade_count >= 2u && is_in_shadow_frustum(v_sun_shadows_pos_1.xyz))
    {
        return sample_shadow(hrz_sun_shadow_map[1], v_sun_shadows_pos_1.xyz);
    }
    else if (shadow_map_cascade_count >= 3u && is_in_shadow_frustum(v_sun_shadows_pos_2.xyz))
    {
        return sample_shadow(hrz_sun_shadow_map[2], v_sun_shadows_pos_2.xyz);
    }
    else if (shadow_map_cascade_count >= 4u && is_in_shadow_frustum(v_sun_shadows_pos_3))
    {
        return sample_shadow(hrz_sun_shadow_map[3], v_sun_shadows_pos_3.xyz);
    }

    return 1.0;
}

#endif // !defined(SHADOWS_DISABLED)

vec3 do_sun_lighting(vec3 normal, vec3 sun_dir, float altitude, vec3 normal_to_ground, bool receive_shadows)
{
    if (hrz_frame.lighting_enabled)
    {
        vec3 ambient = compute_sun_lighting_ambient(normal, sun_dir, altitude, normal_to_ground);
        vec3 diffuse = compute_sun_lighting_diffuse(normal, sun_dir, altitude, normal_to_ground);

#if !defined(SHADOWS_DISABLED)
        if (hrz_frame.receive_shadows && receive_shadows)
        {
            float attenuation_far = smoothstep(
                hrz_frame.shadow_map_far_lin * 0.8,
                hrz_frame.shadow_map_far_lin,
                gl_FragCoord.w);

            diffuse *= 1.0 - (1.0 - apply_shadow_maps(normal, sun_dir)) * attenuation_far;
        }
#endif // !defined(SHADOWS_DISABLED)

        return diffuse + ambient;
    }
    else
    {
        return vec3(1.0);
    }
}

// This version of `do_sun_lighting` may seem redundant, but it can be used without requiring the
// shadow inputs to be set, which is not the case of `do_sun_lighting` even with disabled shadows
// on the web (although it seems to work fine on desktop).
vec3 do_sun_lighting_without_shadows(vec3 normal, vec3 sun_dir, float altitude, vec3 normal_to_ground)
{
    if (hrz_frame.lighting_enabled)
    {
        return compute_sun_lighting_ambient(normal, sun_dir, altitude, normal_to_ground)
            + compute_sun_lighting_diffuse(normal, sun_dir, altitude, normal_to_ground);
    }
    else
    {
        return vec3(1.0);
    }
}
