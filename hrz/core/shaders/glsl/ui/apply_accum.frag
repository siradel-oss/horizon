uniform sampler2D u_accum;
out lowp vec4 o_color;

void main()
{
    o_color = texelFetch(u_accum, ivec2(gl_FragCoord.xy), 0);
}
