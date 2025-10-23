#include "common/backbuffer.glsl"

#define varying in
#include "present/interface.glsl"

#include "present/defs.glsl"

uniform lowp sampler2D u_scene;

layout(location = 0) out lowp vec4 o_color;

void main()
{
    o_color = texelFetch(u_scene, ivec2(v_uv * vec2(hrz_scene.size)), 0);

    // The texture bound to u_scene is already in sRGB space, but there is no
    // way to prevent automatic conversion to linear space when sampling it.
    // So we have to convert back to sRGB manually.
    o_color = convert_color_for_backbuffer(o_color);
}
