layout(location = 0) in vec2 i_in_mesh_pos;
layout(location = 1) in mat4 i_transform; // Goes from 1 to 4
layout(location = 5) in vec2 i_size;
layout(location = 6) in vec4 i_color;
layout(location = 7) in vec4 i_border_color;
layout(location = 8) in vec2 i_border_size_radius;
layout(location = 9) in uint i_anchor_index;

#define varying out
#include "symbol/decorated_shape/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.vert.glsl"
#include "symbol/decorated_shape/defs.glsl"

#include "common/colors.glsl"

void main()
{
    handle_visibility(i_anchor_index);

    Anchor anchor = fetch_anchor(i_anchor_index);

    handle_selection(anchor);

    const float padding = DECORATED_BOX_PADDING;
    v_color = srgb_to_linear(i_color);
    v_color.rgb *= v_color.a;
    v_border_color = srgb_to_linear(i_border_color);
    v_border_color.rgb *= v_border_color.a;
    v_border_radius = i_border_size_radius.y;
    v_border_size = i_border_size_radius.x;
    v_size = i_size * 0.5;

    vec4 in_element_pos = i_transform * vec4((i_in_mesh_pos * padding + 0.5 - padding * 0.5) * i_size, 0, 1);

    // Y axis goes down so invert Y.
    vec2 uv = vec2(i_in_mesh_pos.x, 1.0 - i_in_mesh_pos.y);
    v_uv = write_perspective_uv((uv * 2.0 - 1.0) * padding, in_element_pos.w);

    anchor_vertex(anchor, in_element_pos);
}
