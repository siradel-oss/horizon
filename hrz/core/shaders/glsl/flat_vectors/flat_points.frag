#include "flat_vectors/defs.glsl"
#include "common/colors.glsl"
#include "common/frag_processing.glsl"
#include "common/highlight.glsl"

#define varying in
#include "flat_vectors/interface.glsl"

#include "flat_vectors/common.frag.glsl"

#ifdef FLAT_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef FLAT_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
#endif

#ifdef FLAT_SELECTION
layout(location = 0) out float o_highlight;
#endif

float unit_disc_sdf(vec2 uv)
{
    return length(uv - vec2(0.5));
}

void main()
{
    float dist = unit_disc_sdf(v_uv);

    vec4 disc_color = vec4(v_color.rgb * v_color.a, v_color.a);
    vec4 outline_color = vec4(hrz_tile.disc_outline_color.rgb * hrz_tile.disc_outline_color.a, hrz_tile.disc_outline_color.a);

    disc_color = disc_color * (1.0 - aastep(v_disc_dist, dist));
    outline_color = outline_color * (aastep(v_disc_dist, dist) - aastep(0.5, dist));

    vec4 color = mix_premultiplied_colors(disc_color, outline_color);

    if (color.a == 0.0) discard;

#ifdef FLAT_VISUAL
    o_color = color * min(v_radius_px, 1.0);

    if (build_feature_reference() == hrz_frame.quick_highlight_feature_reference)
    {
        o_color = apply_quick_highlight_color_premultiplied(o_color);
    }
#endif

#ifdef FLAT_PICKING
    o_object_reference.rg = build_object_reference();
#endif

#ifdef FLAT_SELECTION
    o_highlight = 1.0;
#endif
}
