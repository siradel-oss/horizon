// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#include "symbol/defs.glsl"
#include "common/highlight.glsl"
#include "common/logz.glsl"

#define varying in
#include "symbol/interface.glsl"

#ifdef SYMBOL_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef SYMBOL_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
layout(location = 1) out highp float o_depth;
#endif

#ifdef SYMBOL_SELECTION
layout(location = 0) out highp float o_highlight;
#endif

#ifdef SYMBOL_VISUAL
uvec3 build_feature_reference()
{
    return hrz_tile.feature_reference | uvec3(0, v_feature_id);
}
#endif

#ifdef SYMBOL_PICKING
uvec2 build_object_reference()
{
    return hrz_tile.object_reference | uvec2(0, v_feature_index);
}
#endif

void draw_depth(float frag_w, uint z_index)
{
#ifdef SYMBOL_LOG_DEPTH
    gl_FragDepth = log_depth_value(frag_w);
    gl_FragDepth *= 1.0 - 0.00001 * float(z_index);
#endif
}

void draw_depth(uint z_index)
{
    draw_depth(gl_FragCoord.w, z_index);
}

void draw_quick_highlight()
{
#ifdef SYMBOL_VISUAL
    if (build_feature_reference() == hrz_frame.quick_highlight_feature_reference)
    {
        o_color = apply_quick_highlight_color_premultiplied(o_color);
    }
#endif
}

void draw_picking()
{
#ifdef SYMBOL_PICKING
    o_object_reference.rg = build_object_reference();
    o_depth = 1.0 / gl_FragCoord.w;
#endif
}

void draw_selection()
{
#ifdef SYMBOL_SELECTION
    o_highlight = 1.0;
#endif
}

vec2 read_perspective_uv(vec3 uv)
{
    return uv.xy / uv.z;
}

