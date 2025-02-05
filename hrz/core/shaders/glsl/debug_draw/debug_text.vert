#include "common/ubo_frame.glsl"
#include "debug_draw/coordinates.glsl"
#include "debug_draw/defs.glsl"

uniform sampler2D u_instance_data;

layout(location = 0) in vec2 i_pixel_position;
layout(location = 1) in vec2 i_uv;
layout(location = 2) in uint i_instance_index;

out vec2 v_uv;
flat out vec4 v_color;

void main()
{
    v_uv = i_uv;

    ivec2 data_coords = ivec2((int(i_instance_index) % 128) * 3, int(i_instance_index) / 128);

    vec4 position_low_cs = texelFetch(u_instance_data, data_coords + ivec2(0, 0), 0);
    vec3 position_high = texelFetch(u_instance_data, data_coords + ivec2(1, 0), 0).xyz;
    v_color = texelFetch(u_instance_data, data_coords + ivec2(2, 0), 0);

    uint coordinate_space = uint(position_low_cs.w);
    gl_Position = transform(coordinate_space, position_low_cs.xyz, position_high);

    vec2 pixel_offset = i_pixel_position / vec2(hrz_frame.viewport_size) * 2.0;
    gl_Position.xy += pixel_offset * gl_Position.w;
}

