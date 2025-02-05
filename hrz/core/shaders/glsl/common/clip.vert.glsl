#pragma once

#include "common/ubo_frame.glsl"

out float v_clip_distance;
out vec4 v_clip_color;

#if defined(WORKAROUND_004)
// @Workaround(004-Safari-UniformBufferArrayLoad)
ClipPlane get_clip_plane(int index)
{
    if (index < 4)
    {
        if (index < 2)
        {
            if (index == 0) return hrz_frame.clip_planes[0];
            else return hrz_frame.clip_planes[1];
        }
        else
        {
            if (index == 2) return hrz_frame.clip_planes[2];
            else return hrz_frame.clip_planes[3];
        }
    }
    else
    {
        if (index < 6)
        {
            if (index == 4) return hrz_frame.clip_planes[4];
            else return hrz_frame.clip_planes[5];
        }
        else
        {
            if (index == 6) return hrz_frame.clip_planes[6];
            else return hrz_frame.clip_planes[7];
        }
    }
    return hrz_frame.clip_planes[0];
}
#endif // defined(WORKAROUND_004)

void do_clipping(vec4 view_pos, int index)
{
    if (index >= 0 && index < HRZ_S_MAX_CLIP_PLANES)
    {
        ClipPlane clip_plane;

#if !defined(WORKAROUND_004)
        // @Workaround(002-AngleWindows-StructuredBufferNameAndLoading)
        clip_plane.matrix = hrz_frame.clip_planes[index].matrix;
        clip_plane.normal = hrz_frame.clip_planes[index].normal;
        clip_plane.outline_distance = hrz_frame.clip_planes[index].outline_distance;
#else // !defined(WORKAROUND_004)
        // @Workaround(004-Safari-UniformBufferArrayLoad)
        clip_plane = get_clip_plane(index);
#endif // defined(WORKAROUND_004)

        v_clip_distance = dot(vec3(clip_plane.matrix * view_pos), clip_plane.normal) / clip_plane.outline_distance;
    }
    else
    {
        v_clip_distance = 1.0;
    }
}

void compute_clip_outline_attenuation(int index, vec3 view_normal)
{
    if (index >= 0 && index < HRZ_S_MAX_CLIP_PLANES)
    {
        ClipPlane clip_plane;

#if !defined(WORKAROUND_004)
        // @Workaround(002-AngleWindows-StructuredBufferNameAndLoading)
        clip_plane.matrix = hrz_frame.clip_planes[index].matrix;
        clip_plane.normal = hrz_frame.clip_planes[index].normal;
        clip_plane.outline_color = hrz_frame.clip_planes[index].outline_color;
#else // !defined(WORKAROUND_004)
        // @Workaround(004-Safari-UniformBufferArrayLoad)
        clip_plane = get_clip_plane(index);
#endif // defined(WORKAROUND_004)

        v_clip_color = clip_plane.outline_color;
        v_clip_color.rgb *= v_clip_color.a;

        vec3 n = mat3(clip_plane.matrix) * view_normal;
        v_clip_color *= 1.0 - (abs(dot(clip_plane.normal, n)) * 0.75);
    }
    else
    {
        v_clip_color = vec4(0.0);
    }
}
