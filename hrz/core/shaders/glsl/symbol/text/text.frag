// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#define varying in
#include "symbol/text/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.frag.glsl"

#include "symbol/text/defs.glsl"

uniform sampler2D u_font_texture;

// Based on https://chlumsky.appspot.com/msdf-demo
// Coverage to alpha from https://hikogui.org/2022/10/24/the-trouble-with-anti-aliasing.html

#define DEFAULT_TEXT_EDGE 0.5
#define SQRT1_2 0.707106781

const float unit_range = float(HRZ_S_TEXT_SDF_PADDING * 2) / float(HRZ_S_TEXT_TEXTURE_SIZE);

float median(vec3 v)
{
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

float screen_px_range(vec2 uv)
{
    // See https://github.com/Chlumsky/msdfgen/issues/22#issuecomment-237003707
    vec2 dx_uv = dFdx(uv);
    vec2 dy_uv = dFdy(uv);
    float screen_tex_size = length(vec2(length(dx_uv), length(dy_uv))) * SQRT1_2;
    return max(unit_range / screen_tex_size, 1.0);
}

float color_to_lightness(vec3 color)
{
    return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

float coverage_to_alpha(float coverage, float sqrt_foreground)
{
    float coverage_sq = coverage * coverage;
    float coverage_2 = coverage + coverage;
    return mix(coverage_2 - coverage_sq, coverage_sq, sqrt_foreground);
}

void main()
{
    draw_depth(hrz_text.z_index);

#ifdef SYMBOL_VISUAL
#   ifdef SYMBOL_TEXT_FILL
    float text_edge = DEFAULT_TEXT_EDGE;
#   endif
#   ifdef SYMBOL_TEXT_OUTLINE
    float text_edge = DEFAULT_TEXT_EDGE - v_outline_width;
#   endif
#endif

#if defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
    float text_edge = DEFAULT_TEXT_EDGE - v_outline_width;
#endif

    vec2 uv = read_perspective_uv(v_uv).xy;
    float signed_distance = median(texture(u_font_texture, uv).rgb);
    float screen_px_distance = screen_px_range(uv) * (signed_distance - text_edge);
    float coverage = clamp(screen_px_distance + text_edge, 0.0, 1.0);

#ifdef SYMBOL_TEXT_FILL
    float alpha = coverage_to_alpha(coverage, color_to_lightness(v_color.rgb));
#else
    float alpha = coverage;
#endif

#ifdef SYMBOL_VISUAL
    vec4 color = v_color * alpha;
    if (color.a == 0.0) discard;
#elif defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
    if (alpha < 0.25) discard;
#endif

#ifdef SYMBOL_VISUAL
    o_color = color;
#endif

    draw_quick_highlight();
    draw_picking();
    draw_selection();
}
