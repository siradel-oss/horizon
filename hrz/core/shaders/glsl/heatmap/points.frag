#include "heatmap/defs.glsl"
#include "common/frag_processing.glsl"
#include "common/highlight.glsl"

#define varying in
#include "heatmap/interface.glsl"

layout(location = 0) out float o_value;

void main()
{
    // This is like dist from center but smooth around 0.
    float dist = sqrt(v_radius / 10.0 + dot(v_uv, v_uv));

    o_value = v_value * smoothstep(0.0, 1.0, (1.0 - dist / (v_blur_size + v_radius)) * v_radius / v_blur_size);
}
