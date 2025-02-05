#include "common/blend_modes.glsl"

#define varying in
#include "symbol/image/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.frag.glsl"

#include "symbol/image/defs.glsl"

uniform sampler2D u_image;

void main()
{
    draw_depth(hrz_image.z_index);

    vec2 uv = read_perspective_uv(v_uv);
    uv = clamp(uv, vec2(0.0), vec2(1.0)) * v_uv_size + v_uv_offset;

    vec4 color = texture(u_image, uv);
    if (color.a < 0.1) discard;

#ifdef SYMBOL_VISUAL
    o_color = blend_premultiplied(hrz_image.blend_mode, color, v_color.rgb, hrz_image.blend_strength);
    o_color *= v_color.a;
#endif

    draw_quick_highlight();
    draw_picking();
    draw_selection();
}
