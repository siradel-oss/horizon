#include "common/maths.glsl"
#include "common/ubo_frame.glsl"

#include "gltf/defs.glsl"

#define varying in
#include "gltf/interface.glsl"

#ifdef GLTF_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef GLTF_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
layout(location = 1) out highp vec2 o_depth_value;
#endif

#ifdef GLTF_SELECTION
layout(location = 0) out float o_highlight;
#endif

#include "gltf/common.frag.glsl"

void main()
{
    handle_depth_and_clip();

    float value = 0.0;
    vec4 color = compute_material_color(hrz_mesh.geometry.mesh_color_blend_mode, hrz_mesh.geometry.mesh_color_blend_strength, value);

#ifdef GLTF_VISUAL
    o_color = apply_color_decoration(color, get_normal(), hrz_mesh.geometry.feature_reference);
#endif

#ifdef GLTF_PICKING
    o_object_reference.rg = hrz_mesh.geometry.object_reference;
    o_depth_value.x = 1.0 / gl_FragCoord.w;
    o_depth_value.y = value;
#endif

#ifdef GLTF_SELECTION
    o_highlight = 1.0;
#endif

    handle_alpha_discard(color.a);
}
