#define varying in
#include "dev_ui/interface.glsl"

out vec4 o_color;

uniform sampler2D u_atlas;

void main()
{
    float tex_color = texture(u_atlas, v_uv).r;
    o_color = vec4(vec3(1), tex_color) * vec4(v_color);
    o_color.rgb *= o_color.a;
}
