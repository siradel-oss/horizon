#include "common/logz.glsl"
#include "common/ubo_frame.glsl"
#include "common/sun_lighting.frag.glsl"
#include "three_d_tiles/boxes/defs.glsl"

#define varying in
#include "three_d_tiles/boxes/interface.glsl"

out vec4 o_color;

void main()
{
    gl_FragDepth = log_depth_value(gl_FragCoord.w);

    vec3 sun = do_sun_lighting(v_normal, hrz_frame.view_sun_direction, v_altitude, v_normal_to_ground, false);
    vec3 color = pow(v_color.rgb, vec3(0.45)) * sun;
    o_color = vec4(pow(color, vec3(2.2)) * v_color.a, v_color.a);

    ivec2 pixel_coord = ivec2(gl_FragCoord.xy - 0.5);
    if (pixel_coord.x % 2 == pixel_coord.y % 2)
    {
        discard;
    }
}
