// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "common/ubo_frame.glsl"
#include "common/camera_height.glsl"
#include "common/flat_overlay_cameras.glsl"
#include "heatmap/defs.glsl"

layout(location = 0) in vec2 i_in_mesh_pos;
layout(location = 1) in vec3 i_position;
layout(location = 2) in float i_value;
layout(location = 3) in float i_radius;

#define varying out
#include "heatmap/interface.glsl"

void main()
{
    v_value = i_value;

    float radius = i_radius;
    float min_blur_size = 0.1;
    if (!hrz_tile.size_in_meters)
    {
        float pixel_to_meter = fetch_camera_height() * hrz_frame.camera_height_to_perceived_distance * hrz_frame.pixel_size_in_meters;
        radius *= pixel_to_meter;
        min_blur_size = 1.0;
    }

    float blur_size = max(min_blur_size, hrz_tile.blur_size * radius);

    v_blur_size = blur_size;
    v_radius = radius;
	v_uv = i_in_mesh_pos * (radius + blur_size);

    vec4 offset = vec4(translate_relative_to_overlay_cameras(hrz_tile.center_low.xyz, hrz_tile.center_high.xyz), 0.0);
    vec4 vertex = hrz_overlay_cameras.overlay_cams_view_cc * (vec4(i_position, 1.0) + offset);
    vertex.xy += (i_in_mesh_pos * (radius + blur_size));

    vec4 uncorrected = hrz_heatmap_pass.proj * vertex;
    vec4 corrected = uncorrected;

    if (hrz_heatmap_pass.cascade_count > 1u)
    {
        // Use an easing function to remap the point's Y position, so that closer points
        // are left with more space on the texture than far off ones, which will improve their
        // quality when rendering on the flat overlays.
        // Note that we put y in [-0.1; 1.0] and not [0.0, 1.0]: this helps a bit with the stability
        // of the heatmap when looking closely at points on the scale of the screen.
        float y_norm = clamp((uncorrected.y + 1.0) / 2.0, -0.1, 1.0);
        corrected.y = (1.0 - (1.0 - y_norm) * (1.0 - y_norm)) * 2.0 - 1.0;

        // Similarly remap the point's X position by "stretching" the texture horizontally from
        // the center.
        // This will clip some points out of the texture on the sides of the bottom of the texture,
        // but as long as those points were initially positionned outside of the main camera's
        // perspective frustum, the clipping is unnoticeable.
        // The origin is computed to take into account the possible scissor rect applied to the
        // current scene view, which affects the translation component of the heatmap projection.
        float x_origin = hrz_heatmap_pass.proj[3][0];
        float x_scaling = mix(hrz_heatmap_pass.max_scale_factor, 1.0, (corrected.y + 1.0) / 2.0);
        corrected.x = (uncorrected.x - x_origin) * x_scaling + x_origin;
    }

    gl_Position = corrected;
}
