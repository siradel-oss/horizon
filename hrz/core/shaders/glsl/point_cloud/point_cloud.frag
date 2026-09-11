// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#include "common/logz.glsl"
#include "common/ubo_frame.glsl"
#include "common/clip.frag.glsl"
#include "point_cloud/defs.glsl"

#ifdef POINT_CLOUD_VISUAL
#include "common/viewshed.frag.glsl"
#include "common/colors.glsl"
#include "common/sun_lighting.frag.glsl"
#include "common/highlight.glsl"
#endif

#define varying in
#include "point_cloud/interface.glsl"

#ifdef POINT_CLOUD_VISUAL
    layout(location = 0) out vec4 o_color;
#endif

#ifdef POINT_CLOUD_PICKING
    layout(location = 0) out highp uvec2 o_object_reference;
    layout(location = 1) out highp vec2 o_depth_value;
#endif

#ifdef POINT_CLOUD_SELECTION
    layout(location = 0) out float o_highlight;
#endif

void main()
{
#ifdef POINT_CLOUD_LOG_DEPTH
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
#endif
    test_clip();

#ifdef POINT_CLOUD_VISUAL
    vec3 sun = vec3(1.0);
    if (hrz_point_cloud.lighting_enabled)
    {
        sun = do_sun_lighting(v_normal, hrz_frame.view_sun_direction, v_altitude, v_normal_to_ground, hrz_point_cloud.receive_shadows);
    }
    vec4 color = vec4(v_color.rgb * sun, v_color.a);

    color = compute_viewshed_color(color, v_normal);
    color = mix_premultiplied_colors(color, compute_clip_outline_color());

    if ((hrz_point_cloud.feature_reference | uvec3(0, v_feature_id)) == hrz_frame.quick_highlight_feature_reference)
    {
        color = apply_quick_highlight_color_premultiplied(color);
    }

    o_color = color;
#endif

#ifdef POINT_CLOUD_PICKING
    o_object_reference = hrz_point_cloud.object_reference | uvec2(0, v_batch_id + hrz_point_cloud.object_id_offset);

    o_depth_value.x = 1.0 / gl_FragCoord.w;
    o_depth_value.y = 0.0;
#endif

#ifdef POINT_CLOUD_SELECTION
    o_highlight = 1.0;
#endif
}
