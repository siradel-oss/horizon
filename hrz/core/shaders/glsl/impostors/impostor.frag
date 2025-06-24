#include "common/maths.glsl"
#include "common/logz.glsl"
#include "common/viewshed.frag.glsl"
#include "common/clip.frag.glsl"
#include "common/highlight.glsl"
#include "common/blend_modes.glsl"
#include "common/sun_lighting.frag.glsl"
#include "common/ubo_frame.glsl"
#include "common/octahedral.glsl"

#include "impostors/defs.glsl"

#define varying in
#include "impostors/interface.glsl"

#ifdef IMPOSTOR_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef IMPOSTOR_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
layout(location = 1) out highp float o_depth;
#endif

#ifdef IMPOSTOR_SELECTION
layout(location = 0) out highp float o_highlight;
#endif

uniform lowp sampler2D hrz_impostor_texture;
uniform highp sampler2D hrz_impostor_normal_texture;

#ifdef IMPOSTOR_VISUAL
uvec3 build_feature_reference()
{
    return hrz_tile.feature_ref + uvec3(0, v_feature_id);
}
#endif

#ifdef IMPOSTOR_PICKING
uvec2 build_object_reference()
{
    return hrz_tile.object_ref + uvec2(0, v_object_id);
}
#endif

vec4 get_impostor_frame()
{
    vec2 frame_uv = (v_frame + v_uv) / vec2(hrz_impostor.atlas_size);
    return texture(hrz_impostor_texture, frame_uv);
}

vec3 get_normal_frame()
{
    vec2 frame_uv = (v_frame + v_uv) / vec2(hrz_impostor.atlas_size);
    vec2 texel = texture(hrz_impostor_normal_texture, frame_uv).rg;
    // Need to convert from [0;1] to [-1;1] since texture was RG8
    return octahedral_vec2_to_vec3(texel * 2.0 - vec2(1.0));
}

void main()
{
#ifdef IMPOSTOR_LOG_DEPTH
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
#endif
    test_clip();

    vec4 texel = get_impostor_frame();
    if (texel.a < 0.5) discard;

#ifdef IMPOSTOR_VISUAL
    vec3 color = blend_linear(hrz_tile.color_blend_mode, srgb_to_linear(texel), v_color.rgb, hrz_tile.color_blend_strength).rgb;
    if (hrz_tile.lighting_enabled)
    {
        vec3 normal = get_normal_frame();
        mat3 impostor_to_view = mat3(v_impostor_to_view_x, v_impostor_to_view_y, v_impostor_to_view_z);
        color *= do_sun_lighting_without_shadows(impostor_to_view * normal, hrz_frame.view_sun_direction, v_altitude, v_normal_to_ground);
    }

    o_color = vec4(linear_to_srgb(color), v_color.a);
    o_color = compute_viewshed_color_no_correction(o_color);

    if (build_feature_reference() == hrz_frame.quick_highlight_feature_reference)
    {
        o_color = apply_quick_highlight_color(o_color);
    }
#endif

#ifdef IMPOSTOR_PICKING
    o_object_reference.rg = build_object_reference();
    o_depth = 1.0 / gl_FragCoord.w;
#endif

#ifdef IMPOSTOR_SELECTION
    o_highlight = 1.0;
#endif
}
