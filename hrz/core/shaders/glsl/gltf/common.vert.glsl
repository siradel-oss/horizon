#pragma once

#include "gltf/attributes.glsl"
#include "gltf/defs.glsl"
#include "common/clip.vert.glsl"
#include "common/sun_shadows.vert.glsl"
#include "common/viewshed.vert.glsl"
#include "common/camera.glsl"

void output_position(vec4 position_world_cc, vec4 position_view)
{
    do_clipping(position_view, hrz_mesh.geometry.clip_id);

#ifndef GLTF_DEPTH
    gl_Position = hrz_frame.pv_cc_matrix * position_world_cc;
#else
    gl_Position = hrz_view.pv_cc * position_world_cc;
#endif
}

void output_uv_and_color()
{
    v_uv_0 = fetch_uv(i_uv_0, i_compressed_uv_0, hrz_prim.materials[0].uv_compression);
    v_uv_1 = fetch_uv(i_uv_1, i_compressed_uv_1, hrz_prim.materials[1].uv_compression);
    v_geometry_color_lin = fetch_color();
}

#ifdef GLTF_VISUAL
void output_normal(mat3 instance_normal_transform)
{
    vec3 normal = fetch_normal();
    mat3 view_normal_matrix = mat3(hrz_frame.view_matrix);
    v_normal = normalize(view_normal_matrix * instance_normal_transform * hrz_prim.geometry.normal_transform * normal);
}

void output_geometry_decoration(vec3 cc_pos, vec3 view_pos, vec3 view_normal)
{
    do_sun_shadows(vec4(view_pos, 1.0));
    do_viewshed(vec4(view_pos, 1.0));
    compute_clip_outline_attenuation(hrz_mesh.geometry.clip_id, view_normal);

    mat3 view_normal_matrix = mat3(hrz_frame.view_matrix);
    vec3 pos_global = translate_cc_to_global(cc_pos);
    v_altitude = length(pos_global);
    v_normal_to_ground = view_normal_matrix * (pos_global / v_altitude);
}
#endif
