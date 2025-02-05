#pragma once

#include "common/ubo_frame.glsl"

#if !defined(SHADOWS_DISABLED)

// @Note: The planet shader has too many varying variables for some platforms (e.g. Huawei MediaPad
// M5). So, we have to manually pack everything as tight as possible (Especially that we don't know
// for sure that the driver tries to pack varyings internally).
// The following encodes HRZ_S_MAX_SUN_CASCADES(=4) positions by putting the xyz values of the last
// position in the w component of the other vec4.
out vec4 v_sun_shadows_pos_0;
out vec4 v_sun_shadows_pos_1;
out vec4 v_sun_shadows_pos_2;

void do_sun_shadows(vec4 view_pos)
{
    v_sun_shadows_pos_0.xyz = (hrz_frame.sun_matrix[0] * view_pos).xyz;
    v_sun_shadows_pos_1.xyz = (hrz_frame.sun_matrix[1] * view_pos).xyz;
    v_sun_shadows_pos_2.xyz = (hrz_frame.sun_matrix[2] * view_pos).xyz;

    vec3 sun_shadows_pos_3 = (hrz_frame.sun_matrix[3] * view_pos).xyz;
    v_sun_shadows_pos_0.w = sun_shadows_pos_3.x;
    v_sun_shadows_pos_1.w = sun_shadows_pos_3.y;
    v_sun_shadows_pos_2.w = sun_shadows_pos_3.z;
}

#else // !defined(SHADOWS_DISABLED)

#define do_sun_shadows(a)

#endif // defined(SHADOWS_DISABLED)
