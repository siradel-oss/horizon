// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

layout(location = 0) in vec2 i_in_mesh_pos;
layout(location = 1) in mat4 i_transform; // Goes from 1 to 4
layout(location = 5) in vec2 i_size;
layout(location = 6) in vec4 i_color;
layout(location = 7) in uint i_anchor_index;

#define varying out
#include "symbol/placeholder/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.vert.glsl"

#include "common/colors.glsl"

void main()
{
    handle_visibility(i_anchor_index);

    Anchor anchor = fetch_anchor(i_anchor_index);

    handle_selection(anchor);

    v_color = srgb_to_linear(i_color);
    v_color.rgb *= v_color.a;

    vec4 in_element_pos = i_transform * vec4(i_in_mesh_pos * i_size, 0, 1);
    anchor_vertex(anchor, in_element_pos);
}
