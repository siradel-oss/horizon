#include "common/ubo_frame.glsl"
#ifdef EXTRUDED_AUX_VIEW
#include "common/ubo_view.glsl"
#endif
#include "common/camera.glsl"
#include "common/colors.glsl"
#include "common/clip.vert.glsl"
#ifdef EXTRUDED_VISUAL
#include "common/octahedral.glsl"
#include "common/sun_shadows.vert.glsl"
#include "common/viewshed.vert.glsl"
#endif

#include "extruded_vectors/defs.glsl"

layout(location = 0) in vec3 i_position;
layout(location = 1) in uint i_normal;
layout(location = 2) in vec4 i_color;
layout(location = 3) in uint i_feature_index;

uniform highp usampler2D u_selection;
uniform highp usampler2D u_feature_ids;

#define varying out
#include "extruded_vectors/interface.glsl"

bool fetch_selection()
{
    uint bucket_index = i_feature_index / 32u;
    uvec2 coord = uvec2(bucket_index % 2048u, bucket_index / 2048u);
    uint bit_index = i_feature_index % 32u;
    uint bitmask = texelFetch(u_selection, ivec2(coord), 0).r;
    return (bitmask & (1u << bit_index)) != 0u;
}

void main()
{
#ifdef EXTRUDED_SELECTION
    if (!fetch_selection())
    {
        gl_Position = vec4(0);
        return;
    }
#endif

    vec4 pos_model = vec4(translate_relative_to_camera(i_position, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz), 1);
    vec4 view_pos = hrz_frame.view_cc_matrix * pos_model;
    do_clipping(view_pos, hrz_tile.clip_id);
#ifdef EXTRUDED_MAIN_VIEW
    gl_Position = hrz_frame.pv_cc_matrix * pos_model;
#endif

#ifdef EXTRUDED_AUX_VIEW
    gl_Position = hrz_view.pv_cc * pos_model;
#endif

#ifdef EXTRUDED_VISUAL
    mat3 view_normal_matrix = mat3(hrz_frame.view_matrix);

    v_color.rgb = srgb_to_linear(i_color.rgb);
    v_color.a = i_color.a;
    v_normal = view_normal_matrix * octahedral_decompress_normal(i_normal);

    do_sun_shadows(view_pos);
    do_viewshed(view_pos);
    compute_clip_outline_attenuation(hrz_tile.clip_id, v_normal);

    vec3 pos_global = translate(i_position, hrz_tile.center_low.xyz, hrz_tile.center_high.xyz);
    v_altitude = length(pos_global);
    v_normal_to_ground = view_normal_matrix * (pos_global / v_altitude);

    ivec2 data_coords = ivec2(i_feature_index % DATA_TEXTURE_SIZE, i_feature_index / DATA_TEXTURE_SIZE);
    v_feature_id = texelFetch(u_feature_ids, data_coords, 0).rg;
#endif

#ifdef EXTRUDED_PICKING
    v_feature_index = i_feature_index;
#endif
}
