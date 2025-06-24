#include "common/colors.glsl"
#include "common/flat_overlay_cameras.glsl"
#include "common/maths.glsl"
#include "common/ubo_frame.glsl"

#include "gltf/defs.glsl"

#define varying in
#include "three_d_tiles/b3dm/interface.glsl"

#ifdef GLTF_VISUAL
#define FLAT_OVERLAY_VISUAL
#endif
#ifdef GLTF_PICKING
#define FLAT_OVERLAY_PICKING
#endif
#ifdef B3DM_SELECTION
#define FLAT_OVERLAY_SELECTION
#endif

#include "common/flat_overlay_sampling.glsl"

#ifdef GLTF_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef GLTF_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
layout(location = 1) out highp vec2 o_depth_value;
#endif

#ifdef B3DM_SELECTION
layout(location = 0) out float o_highlight;
#endif

#include "gltf/common.frag.glsl"

void main()
{
    handle_depth_and_clip();

    float value = 0.0;
    vec4 color_lin = compute_material_color_lin(
        hrz_mesh.geometry.mesh_color_blend_mode, hrz_mesh.geometry.mesh_color_blend_strength, value);

#ifdef GLTF_VISUAL
    if (hrz_mesh.geometry.draw_under_flat_overlays)
    {
        vec4 color = linear_to_srgb(color_lin);
        color.rgb *= color.a;
        color = mix_premultiplied_colors(color, compute_overlay_color(v_overlay_cams_clip_pos));
        if (color.a > 0.0)
        {
            color.rgb /= color.a;
        }
        color_lin = srgb_to_linear(color);
    }

    uvec3 feature_reference = uvec3(hrz_mesh.geometry.feature_reference.r, v_feature_id);
    o_color = apply_color_decoration(color_lin, get_normal(), feature_reference);
#endif

#ifdef GLTF_PICKING
    o_object_reference = hrz_mesh.geometry.object_reference | uvec2(0, v_batch_id + hrz_mesh.geometry.object_id_offset);

    if (hrz_mesh.geometry.draw_under_flat_overlays)
    {
        // Flat overlays take precedence when drawn over models.
        uvec2 overlay_object_reference = compute_overlay_object_reference(v_overlay_cams_clip_pos);
        if (overlay_object_reference != uvec2(0, 0))
        {
            o_object_reference = overlay_object_reference;
        }
    }

    o_depth_value.x = 1.0 / gl_FragCoord.w;
    o_depth_value.y = value;
#endif

#ifdef B3DM_SELECTION
    o_highlight = 0.0;
    if (v_is_selected != 0u)
    {
        o_highlight = 1.0;
    }
    else if (hrz_mesh.geometry.draw_under_flat_overlays)
    {
        o_highlight = compute_selection_overlay_color(v_overlay_cams_clip_pos);
    }
#endif

    handle_alpha_discard(color_lin.a);
}
