#pragma once

#include "common/colors.glsl"

// Make sure this always matches the values in HrzProtocol.BlendMode
#define BLEND_MODE_NORMAL 0u
#define BLEND_MODE_MULTIPLY 1u
#define BLEND_MODE_SCREEN 2u
#define BLEND_MODE_OVERLAY 3u

vec4 blend_linear(uint mode, vec4 base, vec3 color, float blend_strength)
{
    vec3 blended_color = color;
    if (mode == BLEND_MODE_MULTIPLY)
    {
        blended_color = base.rgb * color;
    }
    else if (mode == BLEND_MODE_SCREEN)
    {
        blended_color = base.rgb + color.rgb * (vec3(1.0) - base.rgb);
    }
    else if (mode == BLEND_MODE_OVERLAY)
    {
        vec3 mult = 2.0 * base.rgb * color.rgb;
        vec3 screen = -1.0 + 2.0 * (base.rgb + color.rgb) - mult;
        blended_color = mix(mult, screen, step(vec3(0.5), base.rgb));
    }
    return vec4(mix(base.rgb, blended_color.rgb, blend_strength), base.a);
}

vec4 blend(uint mode, vec4 base, vec3 color, float blend_strength)
{
    return linear_to_srgb(blend_linear(
        mode,
        srgb_to_linear(base),
        srgb_to_linear(color),
        blend_strength
    ));
}

vec4 blend_premultiplied(uint mode, vec4 base, vec4 color, float blend_strength)
{
    if (base.a != 0.0)
    {
        base.rgb /= base.a;
    }

    if (color.a != 0.0)
    {
        color.rgb /= color.a;
    }

    vec4 blended = blend(
        mode,
        base,
        color.rgb,
        blend_strength
    );

    blended.rgb *= blended.a;
    blended *= color.a;

    return blended;
}
