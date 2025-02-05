layout(location = 0) in vec2 i_vertex;

void main()
{
    gl_Position = vec4(i_vertex, 0, 1);
}
