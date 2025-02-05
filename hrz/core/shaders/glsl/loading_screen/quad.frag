#include "common/frag_processing.glsl"

#define varying in
#include "loading_screen/interface.glsl"

#include "loading_screen/defs.glsl"

layout(location = 0) out vec4 o_color;

uniform sampler2D u_logo;

float sdf(in vec2 p, in vec2 a, in vec2 b)
{
    vec2 pa = p-a, ba = b-a;
    float h = clamp( dot(pa,ba)/dot(ba,ba), 0.0, 1.0 );
    return length( pa - ba*h );
}

void main()
{
    const float width = 0.011;
    const float inner_width = 0.009;
    const float right = 0.7;
    const float bottom = 0.3;

    const vec4 border_color = vec4(0.9, 0.9, 0.9, 1);
    const vec4 blue_color = vec4(0.24, 0.35, 1, 1);
    const vec4 empty_color = vec4(0.2, 0.2, 0.2, 1);
    const vec4 bg_color = vec4(0, 0, 0, 1);

    if (hrz_load.draw_logo)
    {
        o_color = texture(u_logo, vec2(v_uv.x, 1.0 - v_uv.y));
        if (o_color.a <= 0.0) discard;
    }
    else
    {
		vec2 mid_point = vec2(
		    float(hrz_load.viewport_width) * 0.5,
		    float(hrz_load.viewport_height) * 0.3);

		vec2 extent = vec2(float(hrz_load.viewport_width) * 0.5, 0);

		vec2 right = mid_point + extent / 2.0;
		vec2 left = mid_point - extent / 2.0;
		vec2 progress = mix(left, right, v_t);

        float d = sdf(gl_FragCoord.xy, right, left);

		const float radius = 8.0;
		o_color = mix(border_color, bg_color, aastep(radius + 1.5, d));
        o_color = mix(empty_color, o_color, aastep(radius, d));
		o_color = mix(blue_color, o_color, aastep(radius - 0.5, sdf(gl_FragCoord.xy, left, progress)));
    }

    o_color.a *= hrz_load.fadeout;
}
