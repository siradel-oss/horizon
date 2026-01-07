in vec2 i_vertex;
in vec4 i_geometry;
in vec4 i_color;
in uvec4 i_uv;

layout(std140) uniform Uniforms
{
    mat4 projection;
} hrz_dev_ui;

#define varying out
#include "dev_ui/interface.glsl"

#include "common/colors.glsl"

void main()
{
    vec2 vertex = i_vertex * i_geometry.zw + i_geometry.xy;
    gl_Position = hrz_dev_ui.projection * vec4(vertex, 0, 1);
    v_uv = (i_vertex.xy * vec2(i_uv.zw) + vec2(i_uv.xy)) / 128.0;
    v_color = srgb_to_linear(i_color);
    v_color.rgb *= v_color.a;
}
