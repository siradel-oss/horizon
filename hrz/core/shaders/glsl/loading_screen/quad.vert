#include "loading_screen/defs.glsl"

layout(location = 0) in vec2 i_pos;

#define varying out
#include "loading_screen/interface.glsl"

void main()
{
    if (hrz_load.draw_logo)
    {
        vec2 pos = (2.0 * i_pos - 1.0) / 8.0;

        if (hrz_load.viewport_width > hrz_load.viewport_height)
        {
            float aspect_ratio = float(hrz_load.viewport_width) / float(hrz_load.viewport_height);
            gl_Position = vec4(pos * vec2(1.0/aspect_ratio, 1.0), 0, 1.0);
        }
        else
        {
            float aspect_ratio = float(hrz_load.viewport_height) / float(hrz_load.viewport_width);
            gl_Position = vec4(pos * vec2(1.0, 1.0/aspect_ratio), 0, 1.0);
        }
    }
    else
    {
        gl_Position = vec4(2.0 * i_pos - 1.0, 0.0, 1.0);
    }

    v_uv = i_pos;
    v_t = float(hrz_load.num_shaders_ready) / float(hrz_load.num_shaders_total);
}
