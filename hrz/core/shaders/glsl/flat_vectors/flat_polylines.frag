// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "common/colors.glsl"
#include "common/logz.glsl"
#include "common/frag_processing.glsl"
#include "common/highlight.glsl"
#include "common/maths.glsl"
#include "common/polylines.frag.glsl"
#include "flat_vectors/tile_polylines_defs.glsl"
#include "flat_vectors/overlay_passes_defs.glsl"

#define varying in
#include "flat_vectors/interface_polylines.glsl"

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

void main()
{
#ifdef FLAT_POLYLINES_ROUND
    // See the definitions of the varyings in the interface file for an explanation.
    float polyline_h = clamp(dot(v_polyline_pa, v_polyline_ba) * v_polyline_inv_dot_ba_ba, 0.0, 1.0);
    float polyline_distance_2 = length(v_polyline_pa - v_polyline_ba * polyline_h);
    float polyline_distance = min(polyline_distance_2, polyline_distance_2);
    float alpha = aastep(0.5, 1.0 - polyline_distance);
#endif

#ifdef FLAT_POLYLINES_SQUARE
    float alpha = aastep(-0.25, -abs(v_polyline_side));
#endif

    if ((hrz_tile.polyline_sides & uint(HRZ_S_POLYLINE_SIDE_IN)) == 0u)
    {
        alpha *= aastep(0.0, v_polyline_side);
    }

    if ((hrz_tile.polyline_sides & uint(HRZ_S_POLYLINE_SIDE_OUT)) == 0u)
    {

        alpha *= aastep(0.0, -v_polyline_side);
    }

    float dash_value = 1.0;
    if ((hrz_tile.dash_mode == DASH_MODE_FILLED && v_polyline_dash_ratio < 1.0)
        || hrz_tile.dash_mode == DASH_MODE_GRADIENT)
    {
        float dash_progress = fract(v_pos_along_line);
        float dash_start = 1.0 - v_polyline_dash_ratio;

#ifdef FLAT_POLYLINES_ROUND
        // If rounded trails, enforce a minimum trail length to avoid squishing circles when
        // too short
        dash_start = min(dash_start, 1.0 - (v_polyline_dash_tip_radius * 2.0));
#endif

        if (hrz_tile.dash_mode == DASH_MODE_FILLED && dash_start < 1.0)
        {
            dash_value = aastep(dash_start, dash_progress);
        }
        else
        {
            if (v_invert_gradient_direction != 0u)
            {
                dash_progress = 1.0 - fract(v_pos_along_line + 1.0 - dash_start);
            }

            float dash_progress_norm = (dash_progress - dash_start) / (1.0 - dash_start);
            dash_value = clamp(dash_progress_norm, 0.0, 1.0);
        }

#ifdef FLAT_POLYLINES_ROUND
        // The tip origin represents the center of the circle used to round the tip.
        float tip_origin0 = dash_start + v_polyline_dash_tip_radius;
        float tip_origin1 = 1.0 - v_polyline_dash_tip_radius;

        float to_tip_origin0 = tip_origin0 - dash_progress;
        float to_tip_origin1 = tip_origin1 - dash_progress;
        float to_tip_origin = min(abs(to_tip_origin0), abs(to_tip_origin1));

        // Circle SDF to round the tips
        vec2 p = vec2(to_tip_origin, v_polyline_side * 4.0 * v_polyline_dash_tip_radius);
        float distance_to_tip_circle = length(p) - v_polyline_dash_tip_radius;

        // 0 if between the two tip origins, 1 otherwise
        float mask = 1.0 - (step(tip_origin0, dash_progress) * (1.0 - step(tip_origin1, dash_progress)));

        // Using aastep for antialiasing causes an artifact line to appear after the round
        // tips of the dashes. Which is why step is used here instead.
        if (mask != 0.0) dash_value *= (1.0 - step(0.0, distance_to_tip_circle));
#endif
    }

#ifdef FLAT_VISUAL
    // The colours are not premultiplied. If one of the two colours is fully transparent,
    // its non-alpha components will have an effect.
    // This is different from typical blending, and is voluntary.
    vec4 color = oklab_to_linear(mix(v_secondary_color_oklab, v_color, dash_value));

    // The blended colour is now premultiplied.
    color.rgb *= color.a;

    o_color = color * alpha;

    if (build_feature_reference() == hrz_frame.quick_highlight_feature_reference)
    {
        o_color = apply_quick_highlight_color_premultiplied(o_color);
    }
#endif

#if defined(FLAT_PICKING) || defined(FLAT_SELECTION)
    if (alpha == 0.0) discard;
#endif

#ifdef FLAT_PICKING
    o_object_reference.rg = build_object_reference();
#endif

#ifdef FLAT_SELECTION
    o_highlight = 1.0;
#endif
}
