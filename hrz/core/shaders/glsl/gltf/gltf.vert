#define varying out

#include "common/maths.glsl"
#include "common/colors.glsl"
#include "common/ubo_frame.glsl"

#ifdef GLTF_DEPTH
#include "common/ubo_view.glsl"
#endif

#include "gltf/defs.glsl"
#include "gltf/attributes.glsl"

#define varying out
#include "gltf/interface.glsl"

#include "gltf/common.vert.glsl"

void main()
{
    vec4 position = hrz_prim.geometry.transform * vec4(fetch_position(), 1);
    vec4 pos_cc = vec4(translate_relative_to_camera(position.xyz, hrz_prim.geometry.origin_low.xyz, hrz_prim.geometry.origin_high.xyz), 1);
    vec4 view_pos = hrz_frame.view_cc_matrix * pos_cc;

    output_position(pos_cc, view_pos);

#ifdef GLTF_VISUAL
    v_view_pos = view_pos.xyz;

    output_normal(mat3(1.0));
    output_geometry_decoration(pos_cc.xyz, view_pos.xyz, v_normal);
#endif

    output_uv_and_color();
    v_feature_color_lin = hrz_mesh.geometry.mesh_color;
}
