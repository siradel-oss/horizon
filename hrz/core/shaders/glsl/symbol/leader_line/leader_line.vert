// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

layout(location = 0) in vec2 i_in_mesh_pos;
layout(location = 1) in vec3 i_target_in_tile_position;
layout(location = 2) in vec3 i_in_symbol_position;
layout(location = 3) in vec4 i_color;
layout(location = 4) in uint i_anchor_index;

#define varying out
#include "symbol/leader_line/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.vert.glsl"
#include "symbol/leader_line/defs.glsl"

#include "common/colors.glsl"

void main()
{
    handle_visibility(i_anchor_index);

    Anchor anchor = fetch_anchor(i_anchor_index);

    handle_selection(anchor);

    v_color = srgb_to_linear(i_color);
    v_color.rgb *= v_color.a;

#ifdef SYMBOL_VISUAL
    v_feature_id = anchor.feature_id;
#endif

#ifdef SYMBOL_PICKING
    v_feature_index = anchor.feature_index;
#endif

    vec3 target_pos_cc = translate_relative_to_camera(i_target_in_tile_position, hrz_tile.origin_low.xyz, hrz_tile.origin_high.xyz);
    vec4 p0_view = hrz_frame.view_cc_matrix * vec4(target_pos_cc, 1);
    vec4 p1_view = compute_anchored_pos_view(anchor, vec4(i_in_symbol_position, 1));

    float distance_to_anchor = max(0.0, -p0_view.z);
    float pixel_scale_factor = hrz_frame.pixel_size_in_meters * distance_to_anchor;

    float element_scale_factor = ANCHOR_ELEMENT_SIZE_UNIT != SYMBOL_SIZE_UNIT_METERS ? 1.0 : pixel_scale_factor;

    vec4 p0 = hrz_frame.proj_matrix * p0_view;
    vec4 p1 = hrz_frame.proj_matrix * p1_view;

    // Save the w component to write correct values inside the depth buffer.
    float coords_w[2] = float[](1.0 / p0.w, 1.0 / p1.w);
    v_frag_coord_w = coords_w[gl_VertexID / 2];

    p0.xy *= coords_w[0];
    p1.xy *= coords_w[1];

    // Find line normal in 2D space.
    vec2 d = normalize((p1 - p0).xy);
    vec2 n = vec2(-d.y, d.x);
    vec2 width = n * hrz_leader_line.width * hrz_frame.pixel_size_in_clip;

    p0.xy += width * ((i_in_mesh_pos.x * 2.0) - vec2(1.0));
    p1.xy += width * ((i_in_mesh_pos.x * 2.0) - vec2(1.0));

    p0.xy *= p0.w;
    p1.xy *= p1.w;

    gl_Position = mix(p0, p1, vec4(i_in_mesh_pos.y));

    v_uv_x = vec2(i_in_mesh_pos.x * gl_Position.w, gl_Position.w);
}
