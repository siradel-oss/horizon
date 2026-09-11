// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "common/ubo_frame.glsl"

out vec4 v_viewshed_pos[HRZ_S_VIEWSHED_CNT];
out vec3 v_viewshed_dir[HRZ_S_VIEWSHED_CNT];

void do_viewshed(vec4 view_pos)
{
    if(hrz_frame.viewsheds_enabled)
    {
        for (int i = 0; i < HRZ_S_VIEWSHED_CNT; ++i)
        {
            v_viewshed_pos[i] = hrz_frame.vs_pv_matrix[i] * view_pos;
            v_viewshed_dir[i] = normalize(hrz_frame.vs_position_from_main_view[i].xyz - view_pos.xyz);
        }
    }
}
