#include "common/ubo_frame.glsl"
#include "common/camera.glsl"
#include "common/sun_shadows.vert.glsl"
#include "three_d_tiles/boxes/defs.glsl"

layout(location = 0) in vec3 i_vertex;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec4 i_color;

#define varying out
#include "three_d_tiles/boxes/interface.glsl"

void main()
{
    v_normal = normalize(hrz_tile.normal_transform * i_normal);
    gl_Position = hrz_frame.pv_cc_matrix
        * vec4(translate_relative_to_camera(i_vertex, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz), 1);

    v_color = i_color;

    vec3 pos_global = translate(i_vertex, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz);

    mat3 view_normal_matrix = mat3(hrz_frame.view_matrix);
    v_normal = normalize(view_normal_matrix * i_normal);

    v_altitude = length(pos_global);
    v_normal_to_ground = view_normal_matrix * (pos_global / v_altitude);
}
