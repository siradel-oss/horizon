layout(location = 0) out lowp vec4 o_color;

void main()
{
    int px = (int(gl_FragCoord.x) + int(gl_FragCoord.y)) % 2;
    vec3 color = vec3(float(px));
    o_color = vec4(color, 1);
}
