#include "flat_vectors/defs.glsl"

layout(location = 0) in vec2 i_uv;

out vec2 v_uv;

void main()
{
    v_uv = i_uv;

    vec4 vertex = vec4(i_uv * 2.0 - 1.0, 0.0, 1.0);
    gl_Position = hrz_heatmap_quad_overlay.transform[hrz_overlay_passes.pass_id] * vertex;
}
