uniform lowp sampler2D u_peel;
out lowp vec4 o_color;

void main()
{
    o_color = texelFetch(u_peel, ivec2(gl_FragCoord.xy), 0);
}
