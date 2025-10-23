layout(location = 0) in vec4 i_pos_fixed_stretchy;
layout(location = 1) in vec2 i_uv;
layout(location = 2) in mat4 i_transform; // Goes from 2 to 5
layout(location = 6) in vec2 i_stretch_size;
layout(location = 7) in vec4 i_color;
layout(location = 8) in uint i_anchor_index;
layout(location = 9) in vec2 i_uv_offset;
layout(location = 10) in vec2 i_uv_size;

#define varying out
#include "symbol/image/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.vert.glsl"

#include "common/colors.glsl"

void main()
{
    handle_visibility(i_anchor_index);

    Anchor anchor = fetch_anchor(i_anchor_index);

    handle_selection(anchor);

    vec2 pos = i_pos_fixed_stretchy.xy + i_stretch_size * i_pos_fixed_stretchy.zw;

    vec4 in_element_pos = i_transform * vec4(pos, 0, 1);
    anchor_vertex(anchor, in_element_pos);

    v_uv_offset = i_uv_offset;
    v_uv_size = i_uv_size;
    v_uv = write_perspective_uv(i_uv, in_element_pos.w);
    v_color = srgb_to_linear(i_color);
    v_color.rgb *= v_color.a;
}

