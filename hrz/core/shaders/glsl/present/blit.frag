#define varying in
#include "present/interface.glsl"

#include "present/defs.glsl"

uniform lowp sampler2D u_scene;

layout(location = 0) out lowp vec4 o_color;

void main()
{
    o_color = texelFetch(u_scene, ivec2(v_uv * vec2(hrz_scene.size)), 0);
}
