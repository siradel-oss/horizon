// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "planet/dtm_height.glsl"

uniform highp sampler2D u_tessellation;
uniform highp usampler2DArray u_dtm_indirection;
uniform highp sampler2D u_dtm_atlas;

struct InstanceData
{
    vec2 wmerc_base;
    uint geometry_slot;
    float wmerc_scale;
    float pos_scale;
};

// @Workaround(002-AngleWindows-StructuredBufferNameAndLoading)
layout(std140) uniform BinData
{
    InstanceData hrz_planet_bin[HRZ_S_PLANET_BIN_SIZE];
};

layout(location = 0) out float o_height;
layout(location = 1) out vec2 o_normal;

float compute_height(vec2 base_wmerc, vec2 partial_wmerc)
{
    return compute_height_from_dtm(
        u_dtm_indirection, u_dtm_atlas, base_wmerc, partial_wmerc);
}

uniform int my_BaseInstance;

void main()
{
#if defined(WORKAROUND_004)
    // @Workaround(004-Safari-UniformBufferArrayLoad)
    g_clip_center = hrz_planet.clip_center;
#endif

    int instance_id = int(gl_FragCoord.y) - my_BaseInstance;

    // @Workaround(005-Android-LoadDataToStructure)
    vec2 wmerc_base = hrz_planet_bin[instance_id].wmerc_base;
    uint geometry_slot = hrz_planet_bin[instance_id].geometry_slot;
    float wmerc_scale_s = hrz_planet_bin[instance_id].wmerc_scale;
    float pos_scale = hrz_planet_bin[instance_id].pos_scale;

    vec3 wmerc_scale = vec3(0, wmerc_scale_s, -wmerc_scale_s);

    vec4 data1 = texelFetch(u_tessellation, ivec2(int(gl_FragCoord.x) * 2 + 1, int(geometry_slot)), 0);
    vec2 partial_wmerc = data1.zw;

    float height = compute_height(wmerc_base, partial_wmerc);
    float height_posx = compute_height(wmerc_base, partial_wmerc + wmerc_scale.yx);
    float height_negx = compute_height(wmerc_base, partial_wmerc + wmerc_scale.zx);
    float height_posy = compute_height(wmerc_base, partial_wmerc + wmerc_scale.xy);
    float height_negy = compute_height(wmerc_base, partial_wmerc + wmerc_scale.xz);

    vec3 n0 = normalize(vec3(height - height_posx, height_posy - height, pos_scale));
    vec3 n1 = normalize(vec3(height_negx - height, height_posy - height, pos_scale));
    vec3 n2 = normalize(vec3(height_negx - height, height - height_negy, pos_scale));
    vec3 n3 = normalize(vec3(height - height_posx, height - height_negy, pos_scale));

    o_normal = normalize(n0 + n1 + n2 + n3).xy * 0.5 + 0.5;
    o_height = height;
}
