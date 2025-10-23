#pragma once

varying vec4 v_color; // Oklab for polylines, sRGB otherwise

#ifdef FLAT_POINTS
varying vec2 v_uv;
varying float v_disc_dist;
varying float v_radius_px;
#endif

#ifdef FLAT_POLYLINES
varying vec4 v_empty_color_oklab;
varying float v_pos_along_line;

#ifdef FLAT_POLYLINES_ROUND
// See https://www.iquilezles.org/www/articles/distfunctions2d/distfunctions2d.htm, Segment - exact
//
// float sdSegment( in vec2 p, in vec2 a, in vec2 b )
// {
//     vec2 pa = p-a, ba = b-a;
//     float h = clamp( dot(pa,ba)/dot(ba,ba), 0.0, 1.0 );
//     return length( pa - ba*h );
// }
//
// We precompute whatever we can!
varying vec2 v_polyline_pa;
varying vec2 v_polyline_ba;
varying float v_polyline_inv_dot_ba_ba;

flat varying float v_polyline_dash_tip_radius;
#endif

varying float v_polyline_side;
varying float v_polyline_dash_ratio;

flat varying uint v_invert_gradient_direction;
#endif

#if defined(FLAT_POLYGONS) && defined(FLAT_POLYGONS_PATTERN)
varying vec2 v_uv;
varying float v_in_tile_lat;
flat varying float v_camera_height_pattern_scale_factor;
flat varying float v_reference_lat_scale_factor_offset;
flat varying vec2 v_pattern_sprite_size;
flat varying vec2 v_pattern_sprite_offset;
flat varying mat2 v_pattern_transform;
flat varying vec4 v_pattern_color;
flat varying float v_pattern_color_blend_strength;
#endif

#ifdef FLAT_VISUAL
flat varying uvec2 v_feature_id;
#endif

#ifdef FLAT_PICKING
flat varying uint v_feature_index;
#endif
