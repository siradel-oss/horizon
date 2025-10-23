#include "point_cloud/defs.glsl"
#include "common/camera.glsl"
#include "common/colors.glsl"
#include "common/blend_modes.glsl"

#ifdef POINT_CLOUD_VISUAL
#include "common/octahedral.glsl"
#include "common/sun_shadows.vert.glsl"
#include "common/viewshed.vert.glsl"
#endif

#include "common/clip.vert.glsl"

#define varying out
#include "point_cloud/interface.glsl"

layout(location = 0) in vec3 i_position;
layout(location = 1) in vec4 i_color;
layout(location = 2) in uint i_compressed_normal;
layout(location = 3) in uint i_batch_id;

uniform highp usampler2D u_feature_ids;
uniform sampler2D u_feature_colors;
uniform highp usampler2D u_selection;

#define DATA_TEXTURE_WIDTH HRZ_S_POINT_CLOUD_DATA_TEXTURE_WIDTH

uvec2 fetch_feature_id()
{
    uint batch_id = i_batch_id;
    uvec2 coord = uvec2(batch_id % uint(DATA_TEXTURE_WIDTH), batch_id / uint(DATA_TEXTURE_WIDTH));
    return texelFetch(u_feature_ids, ivec2(coord), 0).rg;
}

vec4 fetch_feature_color()
{
    uint batch_id = i_batch_id;
    uvec2 coord = uvec2(batch_id % uint(DATA_TEXTURE_WIDTH), batch_id / uint(DATA_TEXTURE_WIDTH));
    return texelFetch(u_feature_colors, ivec2(coord), 0);
}

bool fetch_selection()
{
    uint bucket_index = i_batch_id / 32u;
    uvec2 coord = uvec2(bucket_index % 2048u, bucket_index / 2048u);
    uint bit_index = i_batch_id % 32u;
    uint bitmask = texelFetch(u_selection, ivec2(coord), 0).r;
    return (bitmask & (1u << bit_index)) != 0u;
}

void main()
{
#ifdef POINT_CLOUD_SELECTION
    if (!fetch_selection())
    {
        gl_Position = vec4(-2.0);
        return;
    }
#endif

    vec4 feature_color = fetch_feature_color();
    v_color = blend(
        hrz_point_cloud.feature_color_blend_mode,
        srgb_to_linear(i_color),
        feature_color.rgb,
        hrz_point_cloud.feature_color_blend_strength);
    v_color.a *= feature_color.a;
    v_color.rgb *= v_color.a;

    if (v_color.a == 0.0)
    {
        gl_Position = vec4(-2.0);
        return;
    }

    vec3 pos_point = hrz_point_cloud.linear_transform * (i_position * hrz_point_cloud.quantized_position_scale + hrz_point_cloud.quantized_position_offset);
    vec4 pos_model = vec4(translate_relative_to_camera(pos_point, hrz_point_cloud.rtc_low.xyz, hrz_point_cloud.rtc_high.xyz), 1);
    vec4 pos_view = hrz_frame.view_cc_matrix * pos_model;

    do_clipping(pos_view, hrz_point_cloud.clip_id);

#ifdef POINT_CLOUD_VISUAL
    vec3 normal = octahedral_decompress_normal(i_compressed_normal, 8u);
    mat3 view_normal_matrix = mat3(hrz_frame.view_matrix);
    v_normal = normalize(view_normal_matrix * hrz_point_cloud.normal_matrix * normal);

    vec3 pos_global = translate(pos_point, hrz_point_cloud.rtc_low.xyz, hrz_point_cloud.rtc_high.xyz);
    v_altitude = length(pos_global);
    v_normal_to_ground = view_normal_matrix * (pos_global / v_altitude);

    do_sun_shadows(pos_view);
    compute_clip_outline_attenuation(hrz_point_cloud.clip_id, v_normal);
    do_viewshed(pos_view);

    v_feature_id = fetch_feature_id();
#endif

#ifdef POINT_CLOUD_PICKING
    v_batch_id = i_batch_id;
#endif

    gl_Position = hrz_frame.pv_cc_matrix * pos_model;
    gl_PointSize = 2.0 * hrz_frame.device_pixel_ratio;

#ifdef POINT_CLOUD_PICKING
    // Make the points a bit larger for picking.
    gl_PointSize *= 2.0;
#endif
}
