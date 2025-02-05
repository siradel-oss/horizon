const vec4 positions[] = vec4[](
    vec4(-1, -1, 0, 1),
    vec4(4, -1, 0, 1),
    vec4(-1, 4, 0, 1)
);

void main()
{
    gl_Position = positions[gl_VertexID];
}
