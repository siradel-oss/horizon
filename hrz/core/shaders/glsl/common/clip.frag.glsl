#pragma once

in float v_clip_distance;
in vec4 v_clip_color;

void test_clip()
{
    if (v_clip_distance < 0.0)
    {
        // Clipping test on every pixels because gl_ClipDistance
        // is not available on WebGL 2.
        discard;
    }
}

vec4 compute_clip_outline_color()
{
    if (v_clip_color.a > 0.0)
    {
        float factor = smoothstep(0.5, 1.0, max(1.0 - v_clip_distance, 0.0));
        return v_clip_color * factor;
    }
    else
    {
        return vec4(0.0);
    }
}
