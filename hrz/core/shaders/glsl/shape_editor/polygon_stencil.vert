#include "common/ubo_frame.glsl"
#include "common/camera.glsl"

layout(location = 0) in vec3 i_position_low;
layout(location = 1) in vec3 i_position_high;

void main()
{
    vec4 pos_cc = vec4(translate_relative_to_camera(vec3(0), i_position_low, i_position_high), 1);
    gl_Position = hrz_frame.pv_cc_matrix * pos_cc;
}
