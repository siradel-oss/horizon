#include "common/logz.glsl"
#include "common/ubo_frame.glsl"
#include "common/maths.glsl"
#include "common/clip.frag.glsl"
#include "common/frag_processing.glsl"
#include "common/highlight.glsl"
#include "common/polylines.frag.glsl"

#include "cylinders/defs.glsl"

#define varying in
#include "cylinders/interface.glsl"

#ifdef CYLINDER_VISUAL
#   include "common/sun_lighting.frag.glsl"
#   include "common/viewshed.frag.glsl"
#endif

#ifdef CYLINDER_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef CYLINDER_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
layout(location = 1) out highp float o_depth;
#endif

#ifdef CYLINDER_SELECTION
layout(location = 0) out float o_highlight;
#endif

#ifdef CYLINDER_VISUAL
uvec3 build_feature_reference()
{
    return hrz_tile.feature_reference | uvec3(0, v_feature_id);
}

vec4 apply_sun_color(vec4 srgb, vec3 sun)
{
    vec3 color = srgb_to_linear(srgb.rgb) * sun;
    return vec4(linear_to_srgb(color), srgb.a);
}
#endif

#ifdef CYLINDER_PICKING
uvec2 build_object_reference()
{
    return hrz_tile.object_reference | uvec2(0, v_feature_index);
}
#endif

void main()
{
#ifdef CYLINDER_LOG_DEPTH
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
#endif
    test_clip();

    float dash_value = 1.0;
    if (hrz_tile.dash_mode != DASH_MODE_DISABLED)
    {
        float dash_progress = fract(v_pos_along_line);
        float dash_start = 1.0 - v_dash_ratio;

        if (hrz_tile.dash_mode == DASH_MODE_FILLED)
        {
            dash_value = step(dash_start, dash_progress);
        }
        else
        {
            if (v_invert_gradient_direction != 0u)
            {
                dash_progress = 1.0 - fract(v_pos_along_line + 1.0 - dash_start);
            }

            float dash_progress_norm = (dash_progress - dash_start) / (1.0 - dash_start);
            dash_value = clamp(dash_progress_norm, 0.0, 1.0);
        }
    }

    vec4 color = mix(v_empty_color, v_color, dash_value);
    color.rgb *= color.a;

    if (color.a == 0.0) discard;

#ifdef CYLINDER_VISUAL
    vec3 sun = vec3(1.0);
    if (hrz_tile.lighting_enabled)
    {
        sun = do_sun_lighting(v_normal, hrz_frame.view_sun_direction, v_altitude, v_normal_to_ground, hrz_tile.receive_shadows);
    }
    o_color = apply_sun_color(color, sun);

    o_color = compute_viewshed_color(o_color, v_normal);
    o_color = mix_premultiplied_colors(o_color, compute_clip_outline_color());

    if (build_feature_reference() == hrz_frame.quick_highlight_feature_reference)
    {
        o_color = apply_quick_highlight_color(o_color);
    }
#endif

#ifdef CYLINDER_PICKING
    o_object_reference.rg = build_object_reference();
    o_depth = 1.0 / gl_FragCoord.w;
#endif

#ifdef CYLINDER_SELECTION
    o_highlight = 1.0;
#endif
}
