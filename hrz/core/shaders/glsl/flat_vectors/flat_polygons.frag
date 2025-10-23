#include "common/colors.glsl"
#include "common/logz.glsl"
#include "common/highlight.glsl"
#include "common/round_to_power_of_two.glsl"
#include "common/blend_modes.glsl"
#include "flat_vectors/defs.glsl"

#define varying in
#include "flat_vectors/interface.glsl"

#include "flat_vectors/common.frag.glsl"

#ifdef FLAT_VISUAL
layout(location = 0) out vec4 o_color;
#endif

#ifdef FLAT_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
#endif

#ifdef FLAT_SELECTION
layout(location = 0) out float o_highlight;
#endif

#ifdef FLAT_POLYGONS_PATTERN
uniform sampler2D u_pattern;
#endif

void main()
{
    vec4 color = v_color;

#ifdef FLAT_POLYGONS_PATTERN
    // Clip tile to bounds, with a tiny margin to avoid missing pixels in some cases.
    if (v_uv.x < -0.01 || v_uv.x > 1.01 || v_uv.y < -0.01 || v_uv.y > 1.01) discard;

    float lat_scale_factor;
    if (hrz_tile.polygon_pattern_tiling_type == POLYGON_PATTERN_FAVOR_SIZE)
    {
        // Scale the pattern along with latitude. Every time the radius of the planet halves, the scale is doubled.
        // This changes per pixel so it cannot be computed in the vertex shader.
        // The scale offset that comes from the reference latitude is applied here.
        // A scale factor less than 1 enlarges the pattern instances.
        float cos_lat = abs(cos(hrz_tile.origin_lat + v_in_tile_lat * hrz_tile.lat_span));
        lat_scale_factor = round_to_power_of_two(1.0 / cos_lat + v_reference_lat_scale_factor_offset);
    }
    else
    {
        lat_scale_factor = 1.0 + v_reference_lat_scale_factor_offset;
    }

    float scale_factor = lat_scale_factor * v_camera_height_pattern_scale_factor;

    // Compute the effective UV for the pattern.
    // When the scale of the pattern is changed, how the pattern instances are placed inside the tile changes too.
    // For exemple if the tile western edge is on a pattern instance boundary at scale 1 does not mean that it is
    // also at an instance boundary at scale 2.
    //
    // Example:
    //     At scale 1:
    //                   tile 0                   tile 1
    //         |------------------------|------------------------|
    //         |0......................1|0......................1|    in-tile UV
    //         |000000001111111122222222|333333334444444455555555|    pattern instances
    //         |0......1        0......1|        0......1        |    texture coordinates
    //         |        0......1        |0......1        0......1|
    //         |------------------------|------------------------|
    //         0                        1                        2    tile origin UV
    //
    //     At scale 2:
    //                   tile 0                   tile 1
    //         |------------------------|------------------------|
    //         |0......................1|0......................1|    in-tile UV
    //         |000000000000000011111111|111111112222222222222222|    pattern instances
    //         |0..............1        |        0..............1|    texture coordinates
    //         |                0....0.5|0.5....1                |
    //         |------------------------|------------------------|
    //         0                        1                        2    tile origin UV
    //
    // As seen for tile n+1 above, in order to correctly compute the texture coordinates inside a tile, the global
    // placement of the pattern instances must be computed: the 0.5 texture coordinate value at scale 2 for tile 1
    // cannot come from 0 / 2, it has to come from (1 + 0) / 2.
    //
    // For instances to be spatially stable, double precision numbers are required. Because they are too heavy, and
    // because texture coordinates are only ever between 0 and 1, modular arithmetic and linear transformation al-
    // gebra are used so that only the UV of the tile origin are needed in double precision. From them the offset
    // (between 0 and 1) to apply to the UV at the origin of the tile are computed, and inside the tile all UVs
    // can be computed in single precision. (We assume large tiles are refined into smaller tiles are the camera
    // comes closer to the ground, and more precision is needed to place pattern instances.)
    //
    // Because the latitude scale factor is computed in this fragment shader, the following computation cannot be
    // made in the vertex shader either.
    vec2 uv_offset = fract(fract(v_pattern_transform * (hrz_tile.origin_uv_low / scale_factor))
        + fract(v_pattern_transform * (hrz_tile.origin_uv_high / scale_factor)));
    vec2 uv = fract(v_pattern_transform * (v_uv / scale_factor) + uv_offset);

    // Translate from in-pattern UV to in-texture UV.
    vec2 in_texture_uv = vec2(uv.x, 1.0 - uv.y);
    in_texture_uv = in_texture_uv * v_pattern_sprite_size + v_pattern_sprite_offset;
    vec4 pattern_color = texture(u_pattern, in_texture_uv);
    pattern_color.rgb *= pattern_color.a;

    // Blend the pattern colour with the pattern texture, then blend the result with the background colour.
    // (This requires the pattern texture to have premultiplied alpha.)
    pattern_color = blend_premultiplied(
        hrz_tile.polygon_pattern_color_blend_mode,
        pattern_color,
        v_pattern_color,
        v_pattern_color_blend_strength);

    color = pattern_color + color * (1.0 - pattern_color.a);
#endif

#ifdef FLAT_VISUAL
    o_color = color;

    if (build_feature_reference() == hrz_frame.quick_highlight_feature_reference)
    {
        o_color = apply_quick_highlight_color_premultiplied(o_color);
    }
#endif

#if defined(FLAT_PICKING) || defined(FLAT_SELECTION)
    if (color.a == 0.0) discard;
#endif

#ifdef FLAT_PICKING
    o_object_reference.rg = build_object_reference();
#endif

#ifdef FLAT_SELECTION
    o_highlight = 1.0;
#endif
}
