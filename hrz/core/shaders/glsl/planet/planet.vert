// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "common/maths.glsl"
#include "common/geo.glsl"
#include "common/ubo_frame.glsl"
#ifdef PLANET_AUX_VIEW
#include "common/ubo_view.glsl"
#endif
#if !defined(PLANET_AUX_VIEW) && !defined(PLANET_FEEDBACK)
#include "common/flat_overlay_cameras.glsl"
#endif

#include "planet/clipmap.glsl"

#define varying out
#include "planet/interface.glsl"

#include "common/sun_shadows.vert.glsl"
#include "common/viewshed.vert.glsl"
#include "common/clip.vert.glsl"

uniform highp sampler2D hrz_planet_tessellation;
uniform highp sampler2D hrz_height_lut;
uniform highp sampler2D hrz_normal_lut;

struct InstanceData
{
    vec2 wmerc_base;
    uint geometry_slot;

    vec4 matrix_row0;
    vec4 matrix_row1;
    vec4 matrix_row2;
};

// @Workaround(002-AngleWindows-StructuredBufferNameAndLoading)
layout(std140) uniform BinData
{
    InstanceData hrz_planet_bin[HRZ_S_PLANET_BIN_SIZE];
};

uniform int my_BaseInstance;

void main()
{
#ifdef PLANET_MAIN_VIEW
    mat4 xform_after_main_view = hrz_frame.proj_matrix;
#endif

#ifdef PLANET_AUX_VIEW
    mat4 xform_after_main_view = hrz_view.pv_from_main_view;
#endif
    // @Workaround(002-AngleWindows-StructuredBufferNameAndLoading)
    InstanceData instance_data;
    instance_data.wmerc_base = hrz_planet_bin[gl_InstanceID].wmerc_base;
    instance_data.geometry_slot = hrz_planet_bin[gl_InstanceID].geometry_slot;
    instance_data.matrix_row0 = hrz_planet_bin[gl_InstanceID].matrix_row0;
    instance_data.matrix_row1 = hrz_planet_bin[gl_InstanceID].matrix_row1;
    instance_data.matrix_row2 = hrz_planet_bin[gl_InstanceID].matrix_row2;

    vec4 data0 = texelFetch(hrz_planet_tessellation, ivec2(gl_VertexID * 2 + 0, instance_data.geometry_slot), 0);
    vec4 data1 = texelFetch(hrz_planet_tessellation, ivec2(gl_VertexID * 2 + 1, instance_data.geometry_slot), 0);

    vec3 pos = data0.xyz;
    vec3 ground_normal = vec3(data0.w, data1.x, data1.y);

    vec2 partial_wmerc = data1.zw;
    vec2 wmerc = partial_wmerc + instance_data.wmerc_base;

    float height = hrz_frame.dtm_enabled
        ? texelFetch(hrz_height_lut, ivec2(gl_VertexID, my_BaseInstance + gl_InstanceID), 0).r
        : 0.0;

    mat4 view_model_matrix = transpose(mat4(
        instance_data.matrix_row0,
        instance_data.matrix_row1,
        instance_data.matrix_row2,
        vec4(0, 0, 0, 1)));

    vec4 model_pos = vec4(pos + ground_normal * height, 1);
    vec4 view_pos = view_model_matrix * model_pos;
    gl_Position = xform_after_main_view * view_pos;

    do_clipping(view_pos, hrz_frame.terrain_clip_id);

#if defined(PLANET_VISUAL) || defined(PLANET_FEEDBACK)
    v_base_wmerc_pos = instance_data.wmerc_base;
    v_partial_wmerc_pos = partial_wmerc;
    v_view_pos = view_pos.xyz;
#endif

#if defined(PLANET_VISUAL) || defined(PLANET_PICKING) || defined(PLANET_SELECTION)
    for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
    {
#if defined(PLANET_PICKING)
        mat4 mvp = hrz_overlay_cameras.overlay_cams_mvp_inv_main_view_picking[i];
#else
        mat4 mvp = hrz_overlay_cameras.overlay_cams_mvp_inv_main_view_visual[i];
#endif
        v_overlay_cams_clip_pos[i] = mvp * view_pos;
    }
#endif

#ifdef PLANET_VISUAL
    do_sun_shadows(view_pos);
    do_viewshed(view_pos);

    v_view_normal_to_sun = mat3(hrz_frame.view_matrix) * ground_normal;

    vec3 normal;
    if (hrz_frame.dtm_enabled)
    {
        vec3 tangent = normalize(vec3(-ground_normal.y, ground_normal.x, 0.0));
        vec3 bitangent = normalize(cross(ground_normal, tangent));
        mat3 local_normal_rotation = mat3(tangent, bitangent, ground_normal);
        vec2 normal_xy = texelFetch(hrz_normal_lut, ivec2(gl_VertexID, my_BaseInstance + gl_InstanceID), 0).rg * 2.0 - vec2(1.0);
        normal = vec3(normal_xy, sqrt(1.0 - dot(normal_xy, normal_xy)));
        normal = local_normal_rotation * normal;
    }
    else
    {
        normal = ground_normal;
    }
    normal = mat3(hrz_frame.view_matrix) * normal;
    v_normal_altitude = vec4(normal, height);

    compute_clip_outline_attenuation(hrz_frame.terrain_clip_id, normal);
#endif
}
