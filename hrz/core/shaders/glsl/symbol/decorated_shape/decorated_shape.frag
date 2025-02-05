#define varying in
#include "symbol/decorated_shape/interface.glsl"

#include "symbol/defs.glsl"
#include "symbol/common.frag.glsl"

#include "symbol/decorated_shape/defs.glsl"

#include "common/frag_processing.glsl"
#include "common/sdf.glsl"

void main()
{
    draw_depth(hrz_decorated_shape.z_index);

    vec2 uv = read_perspective_uv(v_uv);

    float dist = 0.0;
    if (hrz_decorated_shape.shape_type == DECORATED_SHAPE_CIRCLE)
    {
        dist = length(uv * v_size) - min(v_size.x, v_size.y) + v_border_radius;
    }
    else if (hrz_decorated_shape.shape_type == DECORATED_SHAPE_REGULAR_POLYGON)
    {
        dist = sdf_star(uv * v_size, min(v_size.x, v_size.y) - v_border_radius,
            int(hrz_decorated_shape.regular_polygon_sides),
            hrz_decorated_shape.regular_polygon_star);
    }
    else if (hrz_decorated_shape.shape_type == DECORATED_SHAPE_CAPSULE)
    {
        float radius = min(v_size.x, v_size.y);
        vec2 a;
        if (v_size.x > v_size.y)
        {
            a = vec2(v_size.x - radius, 0.0);
        }
        else
        {
            a = vec2(0.0, v_size.y - radius);
        }
        dist = sdf_segment(uv * v_size, a, -a) - radius + v_border_radius;
    }
    else
    {
        dist = sdf_box(uv * v_size, v_size - vec2(v_border_radius));
    }

    float alpha = 1.0 - aastep(v_border_radius, dist);

#ifdef SYMBOL_VISUAL
    float border_alpha = 0.0;
    if (v_border_size > 0.0) border_alpha = aastep(v_border_radius - v_border_size, dist);

    vec4 color = vec4(mix(v_color, v_border_color, border_alpha));
    o_color = vec4(color.rgb, color.a * alpha);
    alpha = o_color.a;
#endif

    if (alpha <= 0.0) discard;

    draw_quick_highlight();
    draw_picking();
    draw_selection();
}
