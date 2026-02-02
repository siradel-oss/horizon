#pragma once

#include "flat_vectors/interface_common.glsl"

varying vec4 v_color; // Oklab

varying vec4 v_secondary_color_oklab;
varying float v_pos_along_line;

varying float v_polyline_side;
varying float v_polyline_dash_ratio;

flat varying uint v_invert_gradient_direction;

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
