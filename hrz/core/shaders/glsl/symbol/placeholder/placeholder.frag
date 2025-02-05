#define varying in
#include "symbol/placeholder/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.frag.glsl"

#include "symbol/placeholder/defs.glsl"

void main()
{
    draw_depth(hrz_placeholder.z_index);

    if (v_color.a == 0.0) discard;

#ifdef SYMBOL_VISUAL
    o_color = v_color;
#endif

    draw_quick_highlight();
    draw_picking();
    draw_selection();
}
