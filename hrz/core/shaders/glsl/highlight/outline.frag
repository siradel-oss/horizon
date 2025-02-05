#include "common/ubo_frame.glsl"

layout(std140) uniform Highlight
{
    vec3 color;
    float fill_alpha;
    float outline_alpha;
    float size;
    float occlusion_alpha;
} hrz_highlight;

layout(location = 0) out vec4 o_color;

uniform sampler2D u_selection;
uniform sampler2D u_scene_depth;
uniform sampler2D u_selection_depth;

void main()
{
    vec2 uv = gl_FragCoord.xy / vec2(hrz_frame.viewport_size);
    vec2 px = hrz_highlight.size * hrz_frame.device_pixel_ratio / vec2(hrz_frame.viewport_size);

    mat3 m = mat3(
        vec3(
            texture(u_selection, uv + vec2(-px.x, -px.y)).x,
            texture(u_selection, uv + vec2(  0.0, -px.y)).x,
            texture(u_selection, uv + vec2( px.x, -px.y)).x),
        vec3(
            texture(u_selection, uv + vec2(-px.x,   0.0)).x,
            texture(u_selection, uv + vec2(  0.0,   0.0)).x,
            texture(u_selection, uv + vec2( px.x,   0.0)).x),
        vec3(
            texture(u_selection, uv + vec2(-px.x,  px.y)).x,
            texture(u_selection, uv + vec2(  0.0,  px.y)).x,
            texture(u_selection, uv + vec2( px.x,  px.y)).x));

    float sobel_x = dot(vec3(1.0, 2.0, 1.0), m[0] - m[2]);

    m = transpose(m);
    float sobel_y = dot(vec3(1.0, 2.0, 1.0), m[0] - m[2]);

    float outline = smoothstep(0.0, 1.0, sqrt(sobel_x * sobel_x + sobel_y * sobel_y) / 4.0);
    float highlight = min(1.0, outline * hrz_highlight.outline_alpha + hrz_highlight.fill_alpha) * m[1].y;

    float depth_scene = textureLod(u_scene_depth, uv, 0.0).r;
    float depth_selection = textureLod(u_selection_depth, uv, 0.0).r;
    if (depth_scene != depth_selection)
    {
        highlight *= hrz_highlight.occlusion_alpha;
    }

    o_color = vec4(hrz_highlight.color, 1.0) * highlight;
}
