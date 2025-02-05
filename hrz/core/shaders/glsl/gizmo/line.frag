#include "common/logz.glsl"
#include "common/frag_processing.glsl"
#include "common/depth_occlusion_effects.frag.glsl"
#include "common/depth_peel.frag.glsl"
#include "defs_line.glsl"

// No perspective divide in X so that the center of the line remains
// at the center and the line is perfectly distributed above and below
// the middle point.
// We simulate noperspective on v_uv_x: https://stackoverflow.com/a/72620057
in vec2 v_uv_x;

// But we do want perspective divide for Y so that it looks like we
// have equal length before and after the middle point.
in float v_uv_y;

out vec4 o_color;

void main()
{
    float uv_x = v_uv_x.x / v_uv_x.y;

    gl_FragDepth = log_depth_value(gl_FragCoord.w);
    depth_peel_discard(gl_FragDepth);

    float fade = smoothstep(0.0, 0.4, v_uv_y) - smoothstep(0.6, 1.0, v_uv_y);
    float aa = aastep(0.25, uv_x) - aastep(0.75, uv_x);

    o_color = hrz_line.color;
    o_color = depth_occlusion_apply_opacity(gl_FragDepth, o_color, 0.25);

    o_color.a *= fade * aa;
    o_color.rgb *= o_color.a;

    if (o_color.a == 0.0) discard;
}
