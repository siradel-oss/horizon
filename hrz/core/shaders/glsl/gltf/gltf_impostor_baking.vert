#define varying out

#include "common/maths.glsl"
#include "common/colors.glsl"

#include "gltf/defs.glsl"
#include "gltf/attributes.glsl"

#define varying out
#include "gltf/interface.glsl"

void main()
{
    vec4 position = hrz_prim.geometry.transform * vec4(fetch_position(), 1);
    gl_Position = hrz_impostor_bake_frame.proj_matrix * hrz_impostor_bake_frame.view_matrix * position;

    v_normal = normalize(hrz_prim.geometry.normal_transform * fetch_normal());
    v_uv_0 = fetch_uv(i_uv_0, i_compressed_uv_0, hrz_prim.materials[0].uv_compression);
    v_uv_1 = fetch_uv(i_uv_1, i_compressed_uv_1, hrz_prim.materials[1].uv_compression);
    v_geometry_color_lin = fetch_color();
    v_feature_color_lin = vec4(1.0);
}
