#include "common/ubo_frame.glsl"
#include "common/camera.glsl"
#include "common/screen_space.glsl"

#include "planet/dtm_height.glsl"

uniform highp usampler2DArray u_dtm_indirection;
uniform highp sampler2D u_dtm_atlas;

layout(location = 0) in vec3 i_position_low;
layout(location = 1) in vec3 i_position_high;
layout(location = 2) in vec2 i_wmerc_low;
layout(location = 3) in vec2 i_wmerc_high;
layout(location = 4) in vec3 i_ground_normal;
layout(location = 5) in vec3 i_local_position;

#define varying out
#include "shape_editor/control_interface.glsl"

#include "shape_editor/control_defs.glsl"

void main()
{
    v_control_id = uint(gl_InstanceID);

    bool is_midpoint_control_point = v_control_id % 2u == 1u;
    if (is_midpoint_control_point && !hrz_control.show_midpoint_control_points)
    {
        // Hide control point by creating zero-sized polygons.
        gl_Position = vec4(0.0);
        return;
    }

    // The elevation is computed for each vertex, though it's the same for the whole mesh.
    // Hopefully the cache takes care of this.
    // Another solution would be to add a pass before this one, that would compute the
    // elevations of all control points and put them in a data texture.
    float elevation = compute_height_from_dtm(u_dtm_indirection, u_dtm_atlas, i_wmerc_high, i_wmerc_low);
    vec3 elevation_offset = elevation * i_ground_normal;

    vec3 control_pos_cc = translate_relative_to_camera(vec3(0.0), i_position_low, i_position_high) + elevation_offset;
    vec4 control_pos_view = hrz_frame.view_cc_matrix * vec4(control_pos_cc, 1.0);
    float scale = hrz_control.control_point_size * hrz_frame.pixel_size_in_meters * -control_pos_view.z;

#ifdef EDITOR_VISUAL
    if (hrz_control.selected_control_point_id == v_control_id)
    {
        // Make the selected control point a tiny bit bigger. This enables keeping it
        // visible when a point has just been appended and the mouse hasn't moved yet.
        // (Otherwise it gets hidden by the appended point's control point.)
        scale *= 1.01;
    }
#endif

#ifdef EDITOR_PICKING
    // Make control points easier to click on, by increasing their picking
    // render size, compared to their visual size.
    scale *= 2.0;
#endif

    vec4 pos_cc = vec4(
        translate_relative_to_camera(
            elevation_offset + i_local_position * scale * 0.5, // * 0.5 because scale is the diameter and we want the radius
            i_position_low,
            i_position_high),
        1);

    gl_Position = hrz_frame.pv_cc_matrix * pos_cc;
}
