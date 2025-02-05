#include "common/maths.glsl"
#include "common/ubo_frame.glsl"

#include "gltf/defs.glsl"

#define varying in
#include "gltf/interface.glsl"

#ifdef GLTF_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef GLTF_PICKING
layout(location = 0) out highp uvec2 o_picking_id;
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
    vec4 color_lin = compute_material_color_lin(hrz_mesh.geometry.mesh_color_blend_mode, hrz_mesh.geometry.mesh_color_blend_strength, value);

#ifdef GLTF_VISUAL
    o_color = apply_color_decoration(color_lin, get_normal(), uvec3(hrz_instance_group.layer_picking_id, v_feature_id));
#endif

#ifdef GLTF_PICKING
    o_picking_id = uvec2(hrz_instance_group.tile_picking_id.r, hrz_instance_group.tile_picking_id.g + v_picking_id + hrz_instance_group.batch_id_offset);
    o_depth_value.x = 1.0 / gl_FragCoord.w;
    o_depth_value.y = value;
#endif

#ifdef GLTF_SELECTION
    o_highlight = 1.0;
#endif

    handle_alpha_discard(color_lin.a);
}
