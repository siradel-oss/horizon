// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#ifdef EDITOR_STENCIL
#include "common/logz.glsl"
#endif

#include "shape_editor/stencil_defs.glsl"

#ifdef EDITOR_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef EDITOR_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
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
    o_object_reference = hrz_shape.object_reference | uvec2(0, 0xffffffffu);
#endif
}
