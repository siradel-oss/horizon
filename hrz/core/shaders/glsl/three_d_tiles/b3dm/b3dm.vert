// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#define varying out

#include "common/colors.glsl"
#include "common/ubo_frame.glsl"

#ifdef B3DM_WITH_OVERLAY
#include "common/flat_overlay_cameras.glsl"
#endif

#ifdef GLTF_DEPTH
#include "common/ubo_view.glsl"
#endif

#include "gltf/defs.glsl"
#include "gltf/attributes.glsl"

#ifdef B3DM_FLOAT_BATCH_IDS
layout(location = 10) in float i_batch_id;
#else
layout(location = 10) in uint i_batch_id;
#endif

#define varying out
#include "three_d_tiles/b3dm/interface.glsl"

#include "gltf/common.vert.glsl"

uniform lowp sampler2D u_color_attribute_texture;
uniform highp usampler2D u_selection_texture;
uniform highp usampler2D u_feature_ids_texture;

#include "common/selection.glsl"
bool fetch_selection()
{
    return fetch_selection_storage(u_selection_texture, uint(i_batch_id));
}

uvec2 fetch_feature_id()
{
    const uint TEXTURE_WIDTH = uint(HRZ_S_B3DM_DATA_TEXTURE_WIDTH);
    uint batch_id = uint(i_batch_id);
    uvec2 coord = uvec2(batch_id % TEXTURE_WIDTH, batch_id / TEXTURE_WIDTH);
    return texelFetch(u_feature_ids_texture, min(ivec2(coord), textureSize(u_feature_ids_texture, 0) - ivec2(1)), 0).rg;
}

vec4 fetch_feature_color()
{
    const uint TEXTURE_WIDTH = uint(HRZ_S_B3DM_DATA_TEXTURE_WIDTH);
    uint batch_id = uint(i_batch_id);
    uvec2 coord = uvec2(batch_id % TEXTURE_WIDTH, batch_id / TEXTURE_WIDTH);
    return texelFetch(u_color_attribute_texture, min(ivec2(coord), textureSize(u_color_attribute_texture, 0) - ivec2(1)), 0);
}

void main()
{
#ifdef B3DM_SELECTION
    v_is_selected = 0u;
    if (fetch_selection())
    {
        v_is_selected = 1u;
    }
    else if (!hrz_mesh.geometry.draw_under_flat_overlays)
    {
        // When the layer is drawn under flat overlays, we cannot discard meshes
        // when they are not selected: they may serve as supporting geometry
        // for selected features drawn on flat overlays.
        // If no features is selected, drawing the mesh is useless, but in this
        // case no selection draw calls are emitted for any layer, so there is
        // need to discard anything here.
        gl_Position = vec4(0);
        return;
    }
#endif

    v_feature_color = fetch_feature_color();
    if (v_feature_color.a == 0.0)
    {
        // The feature is made invisible by the style: generate 0-sized triangles to hide it.
        gl_Position = vec4(0.0);
        return;
    }

    vec4 position = hrz_prim_transform.transform * vec4(fetch_position(), 1.0);
    vec4 pos_cc = vec4(translate_relative_to_camera(position.xyz, hrz_prim_transform.origin_low.xyz, hrz_prim_transform.origin_high.xyz), 1.0);
    vec4 view_pos = hrz_frame.view_cc_matrix * pos_cc;

    output_position(pos_cc, view_pos);

#ifdef B3DM_WITH_OVERLAY
    for (int i = 0; i < HRZ_S_MAX_OVERLAY_CASCADES; i++)
    {
#if defined(GLTF_PICKING)
        mat4 mvp = hrz_overlay_cameras.overlay_cams_mvp_inv_main_view_picking[i];
#else
        mat4 mvp = hrz_overlay_cameras.overlay_cams_mvp_inv_main_view_visual[i];
#endif
        v_overlay_cams_clip_pos[i] = mvp * view_pos;
    }
#endif

#ifdef GLTF_VISUAL
    v_view_pos = view_pos.xyz;

    output_normal(mat3(1.0));
    output_geometry_decoration(pos_cc.xyz, view_pos.xyz, v_normal);

    v_feature_id = fetch_feature_id();
#endif

    output_uv_and_color();
    v_batch_id = uint(i_batch_id);
}
