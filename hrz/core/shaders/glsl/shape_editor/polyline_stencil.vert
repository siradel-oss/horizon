// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#include "common/camera.glsl"
#include "common/camera_height.glsl"
#include "common/geo.glsl"
#include "common/ubo_frame.glsl"

#include "shape_editor/stencil_defs.glsl"

layout(location = 0) in vec3 i_position_low;
layout(location = 1) in vec3 i_position_high;
layout(location = 2) in vec3 i_normal;
layout(location = 3) in vec3 i_bisector;
layout(location = 4) in vec3 i_extrusion_params;

void main()
{
    bool do_extrusion = i_extrusion_params.x != 0.0;
    bool outer_angle_extrusion = i_extrusion_params.y > 0.0;
    bool inner_angle_extrusion = i_extrusion_params.y < 0.0;

    float pixel_to_meter = fetch_camera_height() * hrz_frame.camera_height_to_perceived_distance * hrz_frame.pixel_size_in_meters;
    float line_width = pixel_to_meter * hrz_shape.line_width;
    vec3 position_low = i_position_low;

    if (do_extrusion)
    {
        vec3 perp_offset = i_normal * line_width;
        vec3 bisector_offset = (i_bisector / dot(i_normal, i_bisector)) * line_width;
        vec3 parallel_offset = bisector_offset - perp_offset;
        vec3 offset;
        if (outer_angle_extrusion && length(parallel_offset) > line_width * 2.0)
        {
            offset = perp_offset + normalize(parallel_offset) * line_width * 2.0;
        }
        else if (inner_angle_extrusion)
        {
            offset = perp_offset;
        }
        else
        {
            offset = bisector_offset;
        }
        position_low += offset;
    }

    vec4 pos_cc = vec4(translate_relative_to_camera(vec3(0.0), position_low, i_position_high), 1.0);
    gl_Position = hrz_frame.pv_cc_matrix * pos_cc;
}
