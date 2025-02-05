#include "debug_draw/coordinates.glsl"

layout(location = 0) in vec3 i_position_low;
layout(location = 1) in vec3 i_position_high;
layout(location = 2) in vec4 i_color;
layout(location = 3) in uint i_coordinate_space;

out vec4 v_color;

void main()
{
    v_color = i_color;

    gl_Position = transform(i_coordinate_space, i_position_low, i_position_high);
    gl_PointSize = 6.0;
}
