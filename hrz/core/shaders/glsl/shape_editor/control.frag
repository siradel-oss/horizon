#include "common/logz.glsl"

#define varying in
#include "shape_editor/control_interface.glsl"

#include "shape_editor/control_defs.glsl"

#ifdef EDITOR_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef EDITOR_PICKING
layout(location = 0) out highp uvec2 o_picking_id;
layout(location = 1) out highp float o_depth;
#endif

void main()
{
    gl_FragDepth = log_depth_value(gl_FragCoord.w);

#ifdef EDITOR_VISUAL
    if (hrz_control.selected_control_point_id == v_control_id)
    {
        o_color = hrz_control.control_point_selected_color;
    }
    else if (v_control_id % 2u == 0u)
    {
        o_color = hrz_control.control_point_color;
    }
    else
    {
        o_color = hrz_control.control_point_midpoint_color;
    }
#endif

#ifdef EDITOR_PICKING
    if (hrz_control.selected_control_point_id == v_control_id
        && !hrz_control.pick_selected_control_point)
    {
        discard;
    }

    if (v_control_id % 2u == 1u && !hrz_control.pick_midpoint_control_points)
    {
        discard;
    }

    o_picking_id.r = hrz_control.shape_id;
    o_picking_id.g = v_control_id;
    o_depth = 1.0 / gl_FragCoord.w;
#endif
}
