// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "common/colors.glsl"
#include "common/ubo_frame.glsl"
#include "common/flat_overlay_cameras.glsl"
#include "common/camera_height.glsl"
#include "flat_vectors/tile_points_defs.glsl"
#include "flat_vectors/overlay_passes_defs.glsl"

layout(location = 0) in vec2 i_in_mesh_pos;
layout(location = 1) in vec3 i_position;
layout(location = 2) in vec4 i_color;
layout(location = 3) in float i_radius;
layout(location = 4) in vec4 i_outline_color;
layout(location = 5) in float i_outline_width;
layout(location = 6) in uint i_feature_index;

#include "flat_vectors/common.vert.glsl"

#define varying out
#include "flat_vectors/interface_points.glsl"

void main()
{
#ifdef FLAT_SELECTION
    if (!hrz_tile.base.has_feature_ids || !fetch_selection())
    {
        gl_Position = vec4(0.0);
        return;
    }
#endif

    v_color = srgb_to_linear(i_color);
    v_outline_color = srgb_to_linear(i_outline_color);
    v_uv = i_in_mesh_pos + vec2(0.5);

    float pixel_to_meter = fetch_camera_height() * hrz_frame.camera_height_to_perceived_distance * hrz_frame.pixel_size_in_meters;
    float disc_radius_m = bool(hrz_tile.disc_radius_unit) ? i_radius * pixel_to_meter : i_radius;
    float outline_width_m = bool(hrz_tile.outline_width_unit) ? i_outline_width * pixel_to_meter : i_outline_width;
    float full_radius_m = disc_radius_m + outline_width_m;
    v_radius_px = full_radius_m * float(hrz_overlay_passes.texture_size) / hrz_overlay_passes.world_size;

#ifdef FLAT_VISUAL
    v_feature_id = fetch_feature_id();
#endif

#ifdef FLAT_PICKING
    v_feature_index = i_feature_index;
#endif

    v_disc_dist = (disc_radius_m / full_radius_m) * 0.5;

    vec4 offset = vec4(translate_relative_to_overlay_cameras(hrz_tile.base.center_low.xyz, hrz_tile.base.center_high.xyz), 0.0);
    vec4 vertex = hrz_overlay_cameras.overlay_cams_view_cc * (vec4(i_position, 1.0) + offset);
    vertex.xy += (i_in_mesh_pos * 2.0 * full_radius_m);
    gl_Position = hrz_overlay_cameras.overlay_cams_proj[hrz_overlay_passes.pass_id] * vertex;
}
