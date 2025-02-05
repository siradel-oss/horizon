#include "flat_vectors/defs.glsl"
#include "common/ubo_frame.glsl"

#define PALETTE_FN_NAME apply_palette_heatmap
#define PALETTE_ADDITIONAL_ARGUMENTS
#define PALETTE_UBO_PATH hrz_heatmap_quad_overlay.palette
#include "common/apply_palette.inl.glsl"
#undef PALETTE_FN_NAME
#undef PALETTE_ADDITIONAL_ARGUMENTS
#undef PALETTE_UBO_PATH

layout(location = 0) out vec4 o_color;

in vec2 v_uv;
in float v_bound;

uniform sampler2D u_heatmap;

void main()
{
    vec2 adjusted_uv = v_uv;

    if (hrz_heatmap_quad_overlay.cascade_count > 1u)
    {
        adjusted_uv.y = 1.0 - (1.0 - adjusted_uv.y) * (1.0 - adjusted_uv.y);

        float x_origin = (hrz_heatmap_quad_overlay.proj_translation_x + 1.0) / 2.0;
        float x_scaling = mix(hrz_heatmap_quad_overlay.max_scale_factor, 1.0, adjusted_uv.y);
        adjusted_uv.x = (v_uv.x - x_origin) * x_scaling + x_origin;
    }

    float value = texture(u_heatmap, adjusted_uv).r;

    // Fade out the texture edges to hide the stretched lines due to pixels trying to
    // sample the texture out of bounds being visible on screen (which can happen depending
    // on the camera angle and the terrain height).
    float fade = smoothstep(0.0, 0.02, adjusted_uv.x) * (1.0 - smoothstep(0.98, 1.0, adjusted_uv.x));
    fade *= (1.0 - smoothstep(0.98, 1.0, v_uv.y));
    value *= fade;

    o_color = apply_palette_heatmap(value);
    o_color.rgb *= o_color.a;


    if ((hrz_frame.debug_flags & DEBUG_FLAG_DRAW_HEATMAP_OOB_SAMPLING) != 0u)
    {
        // Highlight areas where the texture is sampled out of bounds in red;
        // Highlight the rest in green
        vec4 good = mix(o_color, vec4(0.0, 1.0, 0.3, 1.0), 0.3);
        vec4 bad = mix(o_color, vec4(1.0, 0.5, 0.5, 1.0), 0.5);
        o_color = mix(bad, good, step(0.001, adjusted_uv.x) - step(0.999, adjusted_uv.x));
    }
}
