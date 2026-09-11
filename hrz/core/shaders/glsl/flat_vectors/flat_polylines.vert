// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "common/camera_height.glsl"
#include "common/flat_overlay_cameras.glsl"
#include "common/octahedral.glsl"
#include "common/polylines.vert.glsl"
#include "common/ubo_frame.glsl"
#include "flat_vectors/tile_polylines_defs.glsl"
#include "flat_vectors/overlay_passes_defs.glsl"

layout(location = 0) in vec3 i_in_mesh_pos;
layout(location = 1) in vec4 i_color;
layout(location = 2) in float i_width;
layout(location = 3) in vec4 i_geometry;
layout(location = 4) in vec3 i_pos0;
layout(location = 5) in vec3 i_pos1;
layout(location = 6) in uint i_normal0;
layout(location = 7) in uint i_normal1;
layout(location = 8) in float i_animation_speed;
layout(location = 9) in vec4 i_secondary_color;
layout(location = 10) in float i_total_length;
layout(location = 11) in uint i_feature_index;

#include "flat_vectors/common.vert.glsl"

#define varying out
#include "flat_vectors/interface_polylines.glsl"

float compute_dash_size_unit_coef(uint dash_size_unit, float pixel_to_meter)
{
    float coef = 1.0;
    if (dash_size_unit == DASH_SIZE_UNIT_PIXELS)
    {
        // This is for the stepping function for polylines dash period sizes trying
        // to enhance stability when zooming in/out or tilting.
        float step = pow(2.0, floor(log2(pixel_to_meter)));
        coef = step;
    }
    else if (dash_size_unit == DASH_SIZE_UNIT_RELATIVE)
    {
        coef = i_total_length;
    }
    return coef;
}

void main()
{
#ifdef FLAT_SELECTION
    if (!hrz_tile.base.has_feature_ids || !fetch_selection())
    {
        gl_Position = vec4(0);
        return;
    }
#endif

#ifdef FLAT_VISUAL
    v_feature_id = fetch_feature_id();
#endif

#ifdef FLAT_PICKING
    v_feature_index = i_feature_index;
#endif

    vec3 normal0 = octahedral_decompress_normal(i_normal0);
    vec3 normal1 = octahedral_decompress_normal(i_normal1);

    float width = i_width;
    v_color = i_color;
    v_secondary_color_oklab = i_secondary_color;
    vec4 offset = vec4(translate_relative_to_overlay_cameras(hrz_tile.base.center_low.xyz, hrz_tile.base.center_high.xyz), 0.0);

    float pixel_to_meter = fetch_camera_height() * hrz_frame.camera_height_to_perceived_distance * hrz_frame.pixel_size_in_meters;
    float line_meter_width = bool(hrz_tile.line_width_unit) ? pixel_to_meter * width : width;

    float px_size = line_meter_width * float(hrz_overlay_passes.texture_size) / hrz_overlay_passes.world_size;
    px_size = max(px_size, 1.0) * 2.0;

    vec4 pos0 = vec4(i_pos0, 1.0) + offset;
    vec4 pos1 = vec4(i_pos1, 1.0) + offset;

    vec4 clip0 = hrz_overlay_cameras.overlay_cams_pv_cc_matrix[hrz_overlay_passes.pass_id] * pos0;
    vec4 clip1 = hrz_overlay_cameras.overlay_cams_pv_cc_matrix[hrz_overlay_passes.pass_id] * pos1;
    vec4 clip_pt = mix(clip0, clip1, i_in_mesh_pos.z);

    vec2 screen0 = (0.5 * clip0.xy + 0.5) * float(hrz_overlay_passes.texture_size);
    vec2 screen1 = (0.5 * clip1.xy + 0.5) * float(hrz_overlay_passes.texture_size);

    float screen_length = length(screen1 - screen0);
    if (screen_length < 0.01)
    {
        // Avoid segments that are too small (less than 1/100th of a pixel)
        // because this will make the following code blow up when generating the
        // joints. Generally this happens when using data that has not been
        // simplified and LODed. Ideally we wouldn't want to handle this case at
        // all but people seem to like ignoring an obscure and taboo thing
        // called floating point precision.
        gl_Position = vec4(-1);
        return;
    }

    float inv_screen_length = 1.0 / screen_length;

    vec2 x_axis = (screen1 - screen0) * inv_screen_length;
    vec2 y_axis = vec2(-x_axis.y, x_axis.x);

    vec2 screen_pt = mix(screen0, screen1, i_in_mesh_pos.z);
    vec2 pt_px = screen_pt + (i_in_mesh_pos.x * x_axis + i_in_mesh_pos.y * y_axis) * float(px_size);

    v_pos_along_line = mix(i_geometry.x, i_geometry.y, i_in_mesh_pos.z);

    float dash_period_unit_coef = compute_dash_size_unit_coef(hrz_tile.dash_period_unit, pixel_to_meter);
    float dash_primary_length_unit_coef = compute_dash_size_unit_coef(hrz_tile.dash_primary_length_unit, pixel_to_meter);
    float animation_unit_coef = compute_dash_size_unit_coef(hrz_tile.animation_speed_unit, pixel_to_meter);

    float dash_meter_period = i_geometry.z * dash_period_unit_coef;
    float animation_advance = i_animation_speed * hrz_frame.time * animation_unit_coef;

    v_pos_along_line = (v_pos_along_line - animation_advance) / dash_meter_period;
    v_invert_gradient_direction = (i_animation_speed < 0.0) ? 1u : 0u;
    v_polyline_dash_ratio = i_geometry.w / i_geometry.z * dash_primary_length_unit_coef / dash_period_unit_coef;

    float offset_along_x_axis = 0.0;

    // When the polyline is too thin, we don't bother with joints because it'll
    // have a minimal impact and it can blow up and create artifacts.
    // This is a bit of a hack, but it's to fix an issue that only happens when
    // we receive bad data (not tiled, not simplified).
    // Note that 4px here is actually a width of 2px because we double the
    // geometry width for antialiasing purposes.
    //      -slerouzic, 2021-11-02
    if (px_size >= 4.0)
    {
        if (i_in_mesh_pos.z < 0.5)
        {
            if (i_normal0 != 0u) // Nice joint, offset to project on joint normal
            {
                // This is OK because we know the transform has uniform scaling, and is an
                // orthographic projection.
                vec2 normal = normalize((hrz_overlay_cameras.overlay_cams_pv_cc_matrix[hrz_overlay_passes.pass_id] * vec4(normal0, 0)).xy);
                offset_along_x_axis = project_point_to_plane_along_direction_distance(vec3(pt_px, 0),  vec3(screen_pt, 0), vec3(normal, 0), vec3(x_axis, 0));
            }
#ifdef FLAT_POLYLINES_ROUND
            else // Not nice joint, or end of polyline, offset slightly to make the tip
            {
                offset_along_x_axis = -float(px_size / 2.0);
            }
#endif
        }
        else
        {
            if (i_normal1 != 0u) // Nice joint, offset to project on joint normal
            {
                // This is OK because we know the transform has uniform scaling, and is an
                // orthographic projection.
                vec2 normal = normalize((hrz_overlay_cameras.overlay_cams_pv_cc_matrix[hrz_overlay_passes.pass_id] * vec4(normal1, 0)).xy);
                offset_along_x_axis = project_point_to_plane_along_direction_distance(vec3(pt_px, 0),  vec3(screen_pt, 0), vec3(normal, 0), vec3(x_axis, 0));
            }
#ifdef FLAT_POLYLINES_ROUND
            else // Not nice joint, or end of polyline, offset slightly to make the tip
            {
                offset_along_x_axis = float(px_size / 2.0);
            }
#endif
        }
    }

    pt_px += x_axis * offset_along_x_axis;

    // Adjust the progress for the deformation due to projection against the joint normal or the joint itself.
    float progress_diff = (i_geometry.y - i_geometry.x) / dash_meter_period;
    float progress_offset = progress_diff * offset_along_x_axis * inv_screen_length;
    v_pos_along_line += progress_offset;

#ifdef FLAT_POLYLINES_ROUND
    // Precompute some things to compute the distance field in the fragment shader later.
    // See the definitions of the varyings in the interface file for an explanation.
    {
        float size_normalize = 2.0 / float(px_size);
        vec2 pa = pt_px.xy - screen0.xy;
        vec2 ba = screen1.xy - screen0.xy;

        v_polyline_pa = pa * size_normalize;
        v_polyline_ba = ba * size_normalize;
        v_polyline_inv_dot_ba_ba = inv_screen_length * inv_screen_length / (size_normalize * size_normalize);
    }

    // Precompute the radius of the circle used for rounding the trail tips.
    v_polyline_dash_tip_radius = line_meter_width / dash_meter_period * 0.5;
#endif

    v_polyline_side = i_in_mesh_pos.y;

    gl_Position = vec4(2.0 * pt_px / float(hrz_overlay_passes.texture_size) - 1.0, clip_pt.zw);
}
