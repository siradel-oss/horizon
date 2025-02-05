layout(location = 0) out highp float o_color;

uniform highp usampler2D u_texture;

void main()
{
    highp uvec4 rgba = texelFetch(u_texture, ivec2(gl_FragCoord.xy), 0);

    // Float conversion + silicium decoding.
    highp uint float_as_u32 = (rgba.r)
        | (rgba.g << 8)
        | ((((rgba.a & 0x01u) << 7) | (rgba.b & 0x7fu)) << 16)
        | ((((rgba.a & 0xfeu) >> 1) | (rgba.b & 0x80u)) << 24);
    o_color = uintBitsToFloat(float_as_u32);
}
