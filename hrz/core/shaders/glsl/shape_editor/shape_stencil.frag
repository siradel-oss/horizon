#ifdef EDITOR_STENCIL
#include "common/logz.glsl"
#endif

#include "shape_editor/stencil_defs.glsl"

#ifdef EDITOR_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef EDITOR_PICKING
layout(location = 0) out highp uvec2 o_picking_id;
#endif

void main()
{
    if (hrz_shape.color.a == 0.0) discard;

#ifdef EDITOR_STENCIL
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
#endif

#ifdef EDITOR_VISUAL
    o_color = hrz_shape.color;
#endif

#ifdef EDITOR_PICKING
    o_picking_id.r = hrz_shape.shape_id;
    o_picking_id.g = 0xffffffffu;
#endif
}
