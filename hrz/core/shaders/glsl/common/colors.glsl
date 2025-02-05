#pragma once

vec3 srgb_to_linear(vec3 x)
{
    return pow(x, vec3(2.2));
}

vec4 srgb_to_linear(vec4 x)
{
    return vec4(srgb_to_linear(x.rgb), x.a);
}

vec3 linear_to_srgb(vec3 x)
{
    return pow(x, vec3(1.0 / 2.2));
}

vec4 linear_to_srgb(vec4 x)
{
    return vec4(linear_to_srgb(x.rgb), x.a);
}

// See 'hrz_common_palette.cpp'.
vec4 oklab_to_srgb(vec4 lms)
{
    vec3 lms_lin = lms.xyz * lms.xyz * lms.xyz;

    mat3 lin_lms_to_lin_rgb = mat3(vec3(+4.0767416621, -1.2684380046, -0.0041960863),
                                   vec3(-3.3077115913, +2.6097574011, -0.7034186147),
                                   vec3(+0.2309699292, -0.3413193965, +1.7076147010));

    vec3 rgb_lin = lin_lms_to_lin_rgb * lms_lin;
    rgb_lin = clamp(rgb_lin, vec3(0), vec3(1));

    return vec4(linear_to_srgb(rgb_lin), lms.a);
}

vec4 mix_premultiplied_colors(vec4 base, vec4 overlay)
{
    return base * (1.0 - overlay.a) + overlay;
}
