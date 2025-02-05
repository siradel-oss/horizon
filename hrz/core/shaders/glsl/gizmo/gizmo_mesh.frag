#include "common/ubo_frame.glsl"
#include "common/logz.glsl"
#include "defs_mesh.glsl"
#include "common/depth_occlusion_effects.frag.glsl"
#include "common/depth_peel.frag.glsl"

in float v_alpha;

out vec4 o_color;

void main()
{
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
    depth_peel_discard(gl_FragDepth);
    o_color = depth_occlusion_apply_opacity(gl_FragDepth, hrz_gizmo.color, 0.3);
    o_color.a *= hrz_gizmo.alpha_fadeout;
    o_color.rgb *= o_color.a;
}
