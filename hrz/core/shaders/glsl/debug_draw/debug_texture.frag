uniform sampler2D u_texture;

in vec2 v_uv;

out vec4 o_color;

void main()
{
    o_color = texture(u_texture, v_uv);
    o_color.rgb *= o_color.a;
}
