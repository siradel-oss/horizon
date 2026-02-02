#pragma once

#include "common/camera.glsl"
#include "common/camera_height.glsl"
#include "common/euler.glsl"
#include "common/octahedral.glsl"
#include "common/ubo_frame.glsl"
#include "symbol/defs.glsl"

#define varying out
#include "symbol/interface.glsl"

uniform highp usampler2D u_anchors;

#define SYMBOL_SIZE_UNIT_METERS 0u
#define SYMBOL_SIZE_UNIT_PIXELS 1u
#define SYMBOL_SIZE_UNIT_PIXELS_RELATIVE_TO_ANCHOR_DISTANCE 2u
#define SYMBOL_SIZE_UNIT_PIXELS_RELATIVE_TO_CAMERA_HEIGHT 3u

// This definition must be in sync with the one in the C++ code.
struct Anchor
{
    vec3 in_tile_pos;
    vec3 pos_offset;
    mat4 rotation;
    uvec2 feature_id;
    uint feature_index;
    vec3 local_east_axis;
    vec3 local_up_axis;
};

// This too
#define ANCHOR_POS_OFFSET_UNIT           (hrz_anchor.flags & 0x0003u)
#define ANCHOR_ALIGN_X_AXIS_TO_SCREEN   ((hrz_anchor.flags & 0x0010u) != 0u)
#define ANCHOR_ALIGN_Y_AXIS_TO_SCREEN   ((hrz_anchor.flags & 0x0020u) != 0u)
#define ANCHOR_ELEMENT_SIZE_UNIT        ((hrz_anchor.flags & 0x000cu) >> 2)
#define ANCHOR_KEEP_UPRIGHT             ((hrz_anchor.flags & 0x0040u) != 0u)

Anchor fetch_anchor(uint anchor_index)
{
    const int pixels_per_anchor = 4;
    ivec2 anchor_data_coords = ivec2(
        (int(anchor_index) % DATA_TEXTURE_SIZE) * pixels_per_anchor,
        int(anchor_index) / DATA_TEXTURE_SIZE);

    uvec4 data0 = texelFetch(u_anchors, anchor_data_coords + ivec2(0, 0), 0);
    uvec4 data1 = texelFetch(u_anchors, anchor_data_coords + ivec2(1, 0), 0);
    uvec4 data2 = texelFetch(u_anchors, anchor_data_coords + ivec2(2, 0), 0);
    uvec4 data3 = texelFetch(u_anchors, anchor_data_coords + ivec2(3, 0), 0);

    Anchor anchor;
    anchor.in_tile_pos = uintBitsToFloat(data0.xyz);
    anchor.pos_offset = uintBitsToFloat(data1.xyz);
    anchor.local_east_axis = octahedral_decompress_normal(data2.w);
    anchor.local_up_axis = octahedral_decompress_normal(data3.x);
    anchor.rotation = euler_angles_xyz_to_mat4(uintBitsToFloat(data2.xyz));
    anchor.feature_index = data3.y;
    anchor.feature_id = data3.zw;

    return anchor;
}

#ifdef SYMBOL_SELECTION
#include "common/selection.glsl"
uniform highp usampler2D u_selection;
bool fetch_selection(uint feature_index)
{
    return fetch_selection_storage(u_selection, feature_index);
}
#endif

#ifdef SYMBOL_SELECTION
#define handle_selection(anchor) if (!fetch_selection(anchor.feature_index)) { gl_Position = vec4(0); return; }
#else
#define handle_selection(anchor) {}
#endif

#define VISIBILITY_TEXTURE_WIDTH 1024u
uniform highp usampler2D u_visibility;

bool fetch_visibility(uint anchor_index)
{
    uint bucket_index = anchor_index / 32u;
    uvec2 coord = uvec2(bucket_index % VISIBILITY_TEXTURE_WIDTH, bucket_index / VISIBILITY_TEXTURE_WIDTH);
    uint bit_index = anchor_index % 32u;
    uint bitmask = texelFetch(u_visibility, ivec2(coord), 0).r;
    return (bitmask & (1u << bit_index)) != 0u;
}

#define handle_visibility(anchor) if (!fetch_visibility(anchor)) { gl_Position = vec4(0); return; }

vec4 perspective_division(vec4 v)
{
    return vec4(
        v.x / v.w,
        v.y / v.w,
        v.z / v.w,
        v.w
    );
}

vec3 write_perspective_uv(vec2 uv, float w)
{
    return vec3(uv / w, 1.0 / w);
}

vec4 compute_anchored_pos_view(const Anchor anchor, vec4 in_element_pos)
{
    vec3 anchor_pos_cc = translate_relative_to_camera(anchor.in_tile_pos, hrz_tile.origin_low.xyz, hrz_tile.origin_high.xyz);

    vec4 anchor_pos_view = hrz_frame.view_cc_matrix * vec4(anchor_pos_cc, 1.0);

    float distance_to_anchor = max(0.0, -anchor_pos_view.z);
    float relative_scale = clamp(
        hrz_anchor.reference_distance / distance_to_anchor,
        hrz_anchor.min_relative_scale,
        hrz_anchor.max_relative_scale);

    float anchor_pixel_scale_factor = hrz_frame.pixel_size_in_meters * distance_to_anchor;

    // A scale factor identical for all anchors in the current view.
    float global_pixel_scale_factor = clamp(
        hrz_frame.pixel_size_in_meters * fetch_camera_height() * hrz_frame.camera_height_to_perceived_distance,
        anchor_pixel_scale_factor * hrz_anchor.min_relative_scale,
        anchor_pixel_scale_factor * hrz_anchor.max_relative_scale);

    float pos_offset_scale_factor = 1.0;
    switch (ANCHOR_POS_OFFSET_UNIT)
    {
        case SYMBOL_SIZE_UNIT_PIXELS:
            pos_offset_scale_factor = anchor_pixel_scale_factor;
            break;

        case SYMBOL_SIZE_UNIT_PIXELS_RELATIVE_TO_ANCHOR_DISTANCE:
            pos_offset_scale_factor = anchor_pixel_scale_factor * relative_scale;
            break;

        case SYMBOL_SIZE_UNIT_PIXELS_RELATIVE_TO_CAMERA_HEIGHT:
            pos_offset_scale_factor = global_pixel_scale_factor;
            break;
    }
    anchor_pos_view += hrz_frame.view_cc_matrix * vec4(anchor.pos_offset * pos_offset_scale_factor, 0.0);

    float element_scale_factor = 1.0;
    switch (ANCHOR_ELEMENT_SIZE_UNIT)
    {
        case SYMBOL_SIZE_UNIT_PIXELS:
            element_scale_factor = anchor_pixel_scale_factor;
            break;

        case SYMBOL_SIZE_UNIT_PIXELS_RELATIVE_TO_ANCHOR_DISTANCE:
            element_scale_factor = anchor_pixel_scale_factor * relative_scale;
            break;

        case SYMBOL_SIZE_UNIT_PIXELS_RELATIVE_TO_CAMERA_HEIGHT:
            element_scale_factor = global_pixel_scale_factor;
            break;
    }

    vec3 canvas_x_axis = vec3(1, 0, 0);
    vec3 canvas_y_axis = vec3(0, -1, 0);

    if (!ANCHOR_ALIGN_X_AXIS_TO_SCREEN || !ANCHOR_ALIGN_Y_AXIS_TO_SCREEN)
    {
        vec3 local_east_axis_view = (hrz_frame.view_cc_matrix * vec4(anchor.local_east_axis, 0)).xyz;
        vec3 local_up_axis_view = (hrz_frame.view_cc_matrix * vec4(anchor.local_up_axis, 0)).xyz;

        if (ANCHOR_ALIGN_X_AXIS_TO_SCREEN)
        {
            canvas_x_axis = vec3(1, 0, 0);
            canvas_y_axis = cross(cross(local_up_axis_view, canvas_x_axis), canvas_x_axis);
        }
        else if (ANCHOR_ALIGN_Y_AXIS_TO_SCREEN)
        {
            canvas_x_axis = normalize(vec3(local_east_axis_view.xy, 0));
            canvas_y_axis = vec3(canvas_x_axis.y, -canvas_x_axis.x, 0);
        }
        else
        {
            canvas_x_axis = local_east_axis_view;
            canvas_y_axis = -local_up_axis_view;
        }
    }

    mat3 transform = mat3(canvas_x_axis, canvas_y_axis, cross(canvas_y_axis, canvas_x_axis))
        * mat3(anchor.rotation)
        * element_scale_factor;

    if (ANCHOR_KEEP_UPRIGHT)
    {
        // We project the X and Y axes of the symbol on the screen to detect if it
        // will appear upside down and flipped so we can correct that.
        vec4 canvas_anchor_proj = hrz_frame.proj_matrix * anchor_pos_view;
        vec4 canvas_x_proj = hrz_frame.proj_matrix * vec4(anchor_pos_view.xyz + transform[0], 1);
        vec4 canvas_y_proj = hrz_frame.proj_matrix * vec4(anchor_pos_view.xyz + transform[1], 1);
        vec3 canvas_anchor_clip = canvas_anchor_proj.xyz / canvas_anchor_proj.w;
        vec2 canvas_x_dir_clip = canvas_x_proj.xy / canvas_x_proj.w - canvas_anchor_clip.xy;
        vec2 canvas_y_dir_clip = canvas_y_proj.xy / canvas_y_proj.w - canvas_anchor_clip.xy;

        // This checks the orientation of the Z axis of the basis with those two vectors as x and y.
        // Essentially We check cross(x, y) > 0.
        if (canvas_x_dir_clip.x * canvas_y_dir_clip.y > canvas_x_dir_clip.y * canvas_y_dir_clip.x)
        {
            // Flipped! Invert the X axis.
            transform[0] = -transform[0];
        }
        else if (canvas_x_dir_clip.x < 0.0)
        {
            // Upside down, rotate by 180° on the symbol's Z axis.
            transform[0] = -transform[0];
            transform[1] = -transform[1];
        }
    }

    in_element_pos = perspective_division(in_element_pos);
    in_element_pos.z = 0.0;
    return anchor_pos_view + vec4(transform * in_element_pos.xyz, 0);
}

void anchor_vertex(const Anchor anchor, vec4 in_element_pos)
{
#ifdef SYMBOL_VISUAL
    v_feature_id = anchor.feature_id;
#endif

#ifdef SYMBOL_PICKING
    v_feature_index = anchor.feature_index;
#endif

    vec4 anchored_pos_view = compute_anchored_pos_view(anchor, in_element_pos);

    gl_Position = hrz_frame.proj_matrix * anchored_pos_view;
}

void discard_vertex()
{
    gl_Position = vec4(0.0);
}
