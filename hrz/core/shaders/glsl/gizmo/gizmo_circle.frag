#include "common/ubo_frame.glsl"
#include "common/logz.glsl"
#include "common/frag_processing.glsl"
#include "defs_circle.glsl"
#include "common/depth_occlusion_effects.frag.glsl"
#include "common/depth_peel.frag.glsl"

in vec2 v_uv;

out vec4 o_color;

void main()
{
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
    depth_peel_discard(gl_FragDepth);

    float half_outline_normalized = hrz_gizmo.circle_outline_size * 0.5 / (hrz_gizmo.circle_radius * CIRCLE_GIZMO_RADIUS_MULTIPLIER);
    float inner_radius = 0.5 - half_outline_normalized;
    float outer_radius = 0.5 + half_outline_normalized;

    float dist = length(v_uv) * (CIRCLE_GIZMO_RADIUS_MULTIPLIER * 0.5);

    vec4 inner_color = hrz_gizmo.circle_inner_color;
    vec4 outer_color = hrz_gizmo.circle_outline_color;

    o_color = mix(inner_color, outer_color, aastep(inner_radius, dist));
    o_color.a *= 1.0 - aastep(outer_radius, dist);

    if (o_color.a == 0.0) discard;

    o_color = depth_occlusion_apply_opacity(gl_FragDepth, o_color, 0.3);
    o_color.rgb *= o_color.a;
}
