#include "common/backbuffer.glsl"

#define varying in
#include "dev_ui/interface.glsl"

out vec4 o_color;

uniform sampler2D u_atlas;

void main()
{
    float tex_color = texture(u_atlas, v_uv).r;
    o_color = vec4(v_color) * tex_color;

    o_color = convert_color_for_backbuffer(o_color);
}
