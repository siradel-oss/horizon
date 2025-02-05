#define varying in
#include "symbol/leader_line/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.frag.glsl"

#include "symbol/leader_line/defs.glsl"

#include "common/frag_processing.glsl"

void main()
{
    draw_depth(gl_FragCoord.w, hrz_leader_line.z_index);

    float uv_x = v_uv_x.x / v_uv_x.y;
    float aa = aastep(0.25, uv_x) - aastep(0.75, uv_x);

#ifdef SYMBOL_VISUAL
    o_color = v_color;
    o_color.a *= aa;
#endif

    draw_quick_highlight();
    draw_picking();
    draw_selection();
}
