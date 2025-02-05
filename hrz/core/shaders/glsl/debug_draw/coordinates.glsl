#pragma once

#include "common/camera.glsl"
#include "common/ubo_frame.glsl"

// Matches `enum class CoordinateSpace` in hrz_core_debug_draw.h
const uint COORDINATE_SPACE_CLIP_SPACE = 0u;
const uint COORDINATE_SPACE_SCREEN_SPACE = 1u;
const uint COORDINATE_SPACE_CAMERA_SPACE = 2u;
const uint COORDINATE_SPACE_ECEF = 3u;

vec4 transform(uint space, vec3 position_low, vec3 position_high)
{
    switch (space)
    {
        default:
        case COORDINATE_SPACE_CLIP_SPACE:
        {
            return vec4(translate(vec3(0), position_low, position_high), 1);
        }

        case COORDINATE_SPACE_SCREEN_SPACE:
        {
            vec4 pos = vec4(translate(vec3(0), position_low, position_high), 1);
            pos.xy = (pos.xy / vec2(hrz_frame.viewport_size) * 2.0) - vec2(1.0, 1.0);
            pos.y *= -1.0;
            return pos;
        }

        case COORDINATE_SPACE_CAMERA_SPACE:
        {
            vec4 pos = vec4(translate(vec3(0), position_low, position_high), 1);
            return hrz_frame.proj_matrix * pos;
        }

        case COORDINATE_SPACE_ECEF:
        {
            vec4 pos_cc = vec4(translate_relative_to_camera(vec3(0), position_low, position_high), 1);
            return hrz_frame.pv_cc_matrix * pos_cc;
        }
    }
}
