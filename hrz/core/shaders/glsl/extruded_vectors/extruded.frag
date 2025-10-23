#include "common/logz.glsl"
#include "common/ubo_frame.glsl"
#include "common/maths.glsl"
#include "common/clip.frag.glsl"
#include "common/highlight.glsl"

#ifdef EXTRUDED_VISUAL
#include "common/sun_lighting.frag.glsl"
#include "common/viewshed.frag.glsl"
#endif

#include "extruded_vectors/defs.glsl"

#define varying in
#include "extruded_vectors/interface.glsl"

#ifdef EXTRUDED_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef EXTRUDED_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
layout(location = 1) out highp float o_depth;
#endif

#ifdef EXTRUDED_SELECTION
layout(location = 0) out float o_highlight;
#endif

#ifdef EXTRUDED_VISUAL
uvec3 build_feature_reference()
{
    return hrz_tile.feature_reference | uvec3(0, v_feature_id);
}
#endif

#ifdef EXTRUDED_PICKING
uvec2 build_object_reference()
{
    return hrz_tile.object_reference | uvec2(0, v_feature_index);
}
#endif

void main()
{
#ifdef EXTRUDED_LOG_DEPTH
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
#endif
    test_clip();

#ifdef EXTRUDED_VISUAL
    vec3 normal = normalize(v_normal);
    vec3 sun = vec3(1.0);
    if (hrz_tile.lighting_enabled)
    {
        sun = do_sun_lighting(normal, hrz_frame.view_sun_direction, v_altitude, v_normal_to_ground, hrz_tile.receive_shadows);
    }

    vec3 color = v_color.rgb * sun;
    o_color = vec4(linear_to_srgb(color), v_color.a);
    o_color.rgb *= o_color.a;

    o_color = compute_viewshed_color(o_color, normal);
    o_color = mix_premultiplied_colors(o_color, compute_clip_outline_color());

    if (build_feature_reference() == hrz_frame.quick_highlight_feature_reference)
    {
        o_color = apply_quick_highlight_color(o_color);
    }
#endif

#ifdef EXTRUDED_PICKING
    o_object_reference.rg = build_object_reference();
    o_depth = 1.0 / gl_FragCoord.w;
#endif

#ifdef EXTRUDED_SELECTION
    o_highlight = 1.0;
#endif
}
