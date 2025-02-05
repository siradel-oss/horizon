#include "common/logz.glsl"

out vec4 o_color;

void main()
{
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
    o_color = vec4(1);
}
