#include "common/frag_processing.glsl"

#define varying in
#include "symbol/text/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.frag.glsl"

#include "symbol/text/defs.glsl"

#define DEFAULT_TEXT_EDGE 0.5

float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

uniform sampler2D u_font_texture;

void main()
{
    draw_depth(hrz_text.z_index);

#ifdef SYMBOL_VISUAL
#   ifdef SYMBOL_TEXT_FILL
    float text_edge = DEFAULT_TEXT_EDGE;
#   endif
#   ifdef SYMBOL_TEXT_OUTLINE
    float text_edge = DEFAULT_TEXT_EDGE - v_outline_width;
#   endif
#endif

#if defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
    float text_edge = DEFAULT_TEXT_EDGE - v_outline_width;
#endif

    vec3 tex_value = texture(u_font_texture, read_perspective_uv(v_uv)).rgb;
    float dist = median(tex_value.r, tex_value.g, tex_value.b);
    float alpha = aastep(text_edge, dist);

#ifdef SYMBOL_VISUAL
    vec4 color = v_color * alpha;
    if (color.a == 0.0) discard;
#elif defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
    if (alpha < 0.25) discard;
#endif

#ifdef SYMBOL_VISUAL
    o_color = color;
#endif

    draw_quick_highlight();
    draw_picking();
    draw_selection();
}
