#pragma once

// sRGB <-> Linear conversion functions from Godot
// https://github.com/godotengine/godot/blob/c5cf73a2e7abe0dae858bc47408d57700f7c2845/drivers/gles3/shaders/tonemap_inc.glsl#L13
// MIT license

// This expects 0-1 range input, outside that range it behaves poorly.
vec3 srgb_to_linear(vec3 color) {
	// Approximation from http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	return color * (color * (color * 0.305306011 + 0.682171111) + 0.012522878);
}

vec4 srgb_to_linear(vec4 color)
{
    return vec4(srgb_to_linear(color.rgb), color.a);
}

// This expects 0-1 range input.
vec3 linear_to_srgb(vec3 color) {
	// Approximation from http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	return max(vec3(1.055) * pow(color, vec3(0.416666667)) - vec3(0.055), vec3(0.0));
}

vec4 linear_to_srgb(vec4 color)
{
    return vec4(linear_to_srgb(color.rgb), color.a);
}

// See 'hrz_common_color.cpp'.
vec3 oklab_to_linear(vec3 lms)
{
    vec3 lms_lin = lms.xyz * lms.xyz * lms.xyz;

    mat3 lin_lms_to_lin_rgb = mat3(vec3(+4.0767416621, -1.2684380046, -0.0041960863),
                                   vec3(-3.3077115913, +2.6097574011, -0.7034186147),
                                   vec3(+0.2309699292, -0.3413193965, +1.7076147010));

    vec3 rgb_lin = lin_lms_to_lin_rgb * lms_lin;
    rgb_lin = clamp(rgb_lin, vec3(0), vec3(1));

    return rgb_lin;
}

vec4 oklab_to_linear(vec4 lmsa)
{
    return vec4(oklab_to_linear(lmsa.rgb), lmsa.a);
}

vec3 oklab_to_srgb(vec3 lms)
{
    return linear_to_srgb(oklab_to_linear(lms));
}

vec4 oklab_to_srgb(vec4 lmsa)
{
    return vec4(oklab_to_srgb(lmsa.rgb), lmsa.a);
}

vec4 mix_premultiplied_colors(vec4 base, vec4 overlay)
{
    return base * (1.0 - overlay.a) + overlay;
}
