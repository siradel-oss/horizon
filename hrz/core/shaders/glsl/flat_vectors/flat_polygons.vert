#include "common/ubo_frame.glsl"
#include "common/flat_overlay_cameras.glsl"
#include "common/round_to_power_of_two.glsl"
#include "flat_vectors/defs.glsl"

#ifdef FLAT_POLYGONS_PATTERN
#include "common/camera_height.glsl"
#endif

layout(location = 0) in vec3 i_in_tile_pos;
#ifndef FLAT_POLYGONS_PATTERN
layout(location = 1) in vec4 i_color;
#endif
#ifdef FLAT_POLYGONS_PATTERN
layout(location = 2) in vec2 i_uv;
layout(location = 3) in float i_in_tile_lat;
layout(location = 4) in uint i_pattern_style_index;
#endif
layout(location = 5) in uint i_feature_index;

#include "flat_vectors/common.vert.glsl"

#ifdef FLAT_POLYGONS_PATTERN
uniform highp usampler2D u_pattern_styles;

// Equivalent to unpackUnorm4x8 from later versions of GLSL.
vec4 unpackColor(uint p)
{
    return vec4(
        float(p & uint(0x000000ff)),
        float(p & uint(0x0000ff00)),
        float(p & uint(0x00ff0000)),
        float(p & uint(0xff000000))
    ) * vec4(
        1.0 / 255.0,
        1.0 / (256.0 * 255.0),
        1.0 / (256.0 * 256.0 * 255.0),
        1.0 / (256.0 * 256.0 * 256.0 * 255.0)
    );
}

void fetch_pattern_style(uint style_index)
{
    const uint DATA_TEXTURE_SIZE = 512u;
    const int pixels_per_style = 3;

    ivec2 style_data_coords = ivec2(
        int(style_index % DATA_TEXTURE_SIZE) * pixels_per_style,
        int(style_index / DATA_TEXTURE_SIZE));

    uvec4 data0 = texelFetch(u_pattern_styles, style_data_coords + ivec2(0, 0), 0);
    uvec4 data1 = texelFetch(u_pattern_styles, style_data_coords + ivec2(1, 0), 0);
    uvec4 data2 = texelFetch(u_pattern_styles, style_data_coords + ivec2(2, 0), 0);

    vec2 sprite_size = uintBitsToFloat(data0.xy);

    v_pattern_sprite_size = sprite_size;
    v_pattern_sprite_offset = uintBitsToFloat(data0.zw);
    v_pattern_transform[0].xy = uintBitsToFloat(data1.xy);
    v_pattern_transform[1].xy = uintBitsToFloat(data1.zw);
    v_color = unpackColor(data2.x);
    v_pattern_color = unpackColor(data2.y);
    v_pattern_color.rgb *= v_pattern_color.a;
    v_pattern_color_blend_strength = uintBitsToFloat(data2.z);

    if (sprite_size.x == 0.0 || sprite_size.y == 0.0)
    {
        // Avoid artefacts due to invalid size and ignore pattern
        v_pattern_color.a = 0.0;
    }
}
#endif

void main()
{
#ifdef FLAT_SELECTION
    if (!fetch_selection())
    {
        gl_Position = vec4(0);
        return;
    }
#endif

#ifndef FLAT_POLYGONS_PATTERN
    v_color = i_color;
#endif

#ifdef FLAT_VISUAL
    v_feature_id = fetch_feature_id();
#endif

#ifdef FLAT_PICKING
    v_feature_index = i_feature_index;
#endif

    vec4 offset = vec4(translate_relative_to_overlay_cameras(hrz_tile.center_low.xyz, hrz_tile.center_high.xyz), 0);
    gl_Position = hrz_overlay_cameras.overlay_cams_pv_cc_matrix[hrz_overlay_passes.pass_id] * (vec4(i_in_tile_pos, 1) + offset);

#ifdef FLAT_POLYGONS_PATTERN
    v_uv = i_uv;
    v_in_tile_lat = i_in_tile_lat;
    fetch_pattern_style(i_pattern_style_index);

    if (hrz_tile.polygon_pattern_unit == POLYGON_PATTERN_SIZE_IN_PIXELS
        || hrz_tile.polygon_pattern_unit == POLYGON_PATTERN_SIZE_RELATIVE_TO_SPRITE_IN_PIXELS)
    {
        v_camera_height_pattern_scale_factor =
            round_to_power_of_two(fetch_camera_height() * hrz_frame.camera_height_to_perceived_distance * hrz_frame.pixel_size_in_meters);
    }
    else
    {
        v_camera_height_pattern_scale_factor = 1.0;
    }

    if (hrz_tile.polygon_pattern_tiling_type == POLYGON_PATTERN_FAVOR_SIZE)
    {
        if (hrz_tile.polygon_pattern_ref_lat_type == POLYGON_PATTERN_DYNAMIC_REFERENCE_LATITUDE)
        {
            float reference_lat = hrz_frame.view_latitude;
            float reference_lat_size_factor = 1.0 / abs(cos(reference_lat));
            v_reference_lat_scale_factor_offset =
                round_to_power_of_two(reference_lat_size_factor) - reference_lat_size_factor;
        }
        else
        {
            v_reference_lat_scale_factor_offset = hrz_tile.polygon_pattern_reference_lat_scale_factor_offset;
        }
    }
    else
    {
        if (hrz_tile.polygon_pattern_ref_lat_type == POLYGON_PATTERN_DYNAMIC_REFERENCE_LATITUDE)
        {
            float reference_lat = hrz_frame.view_latitude;
            float reference_lat_size_factor = 1.0 / abs(cos(reference_lat));
            v_reference_lat_scale_factor_offset = round_to_power_of_two(reference_lat_size_factor);
        }
        else
        {
            v_reference_lat_scale_factor_offset = hrz_tile.polygon_pattern_reference_lat_scale_factor_offset;
        }
    }
#endif
}
