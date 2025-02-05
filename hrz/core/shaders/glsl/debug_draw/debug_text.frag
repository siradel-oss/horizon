#include "common/logz.glsl"
#include "debug_draw/defs.glsl"

uniform sampler2D u_font_atlas;

in vec2 v_uv;
flat in vec4 v_color;

out vec4 o_color;

void main()
{
    gl_FragDepth = log_depth_value(gl_FragCoord.w);

    float tex = texture(u_font_atlas, v_uv).r;

    // Prevent black artifacts due to linear filtering on the glyph edges
    o_color.rgb = vec3(step(0.01, tex)) * v_color.rgb;
    o_color.a = tex * v_color.a;
}
