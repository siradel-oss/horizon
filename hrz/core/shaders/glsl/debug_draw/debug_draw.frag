#include "common/logz.glsl"

in vec4 v_color;

out vec4 o_color;

void main()
{
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
    o_color = v_color;
}
