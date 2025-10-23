#ifdef WRITE_SRGB_TO_BACKBUFFER
#include "common/colors.glsl"
#endif

vec4 convert_color_for_backbuffer(vec4 color)
{
#ifdef WRITE_SRGB_PREMULTIPLIED_ALPHA_TO_BACKBUFFER
    // Web browsers allow writing non-premultiplied alpha to the canvas,
    // but it seems less well supported, and it generates some artifacts
    // when clipping is involved (e.g. with CSS) on Firefox.
    // When they expect premultiplied alpha, they want sRGB premultiplied
    // alpha, so we have to un-premultiply, then convert to sRGB, then
    // re-premultiply.
    if (color.a > 0.0)
    {
        color.rgb /= color.a;
    }
#endif

#ifdef WRITE_SRGB_TO_BACKBUFFER
    color = linear_to_srgb(color);
#endif

#ifdef WRITE_SRGB_PREMULTIPLIED_ALPHA_TO_BACKBUFFER
    color.rgb *= color.a;
#endif

    return color;
}
