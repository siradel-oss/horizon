// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "common/maths.glsl"
#include "common/octahedral.glsl"
#include "gltf/defs.glsl"

#define varying in
#include "gltf/interface.glsl"

layout(location = 0) out vec4 o_color;
layout(location = 1) out vec2 o_normal;

#include "gltf/common.frag.glsl"

void main()
{
    float value = 0.0;
    vec4 tex_color = compute_texture_color(0, hrz_material_texture_0, v_uv_0, value);

    // We use the original alpha instead of the one we might have applied
    // cutoff to because other it messes with the alpha cutoff performed during
    // the impostor rendering. For instance, foliage with low opacity will not
    // be rendered and create trees without leaves.
    vec4 color = v_geometry_color * tex_color * hrz_prim_draw.materials[0].material_color * hrz_mesh.geometry.mesh_color.a;

    vec4 material_color = compute_material_color(0, tex_color);
    handle_alpha_discard(material_color.a);

    o_color = color;

    // Normal texture is RG8, so the encoded value should be converted from [-1;1] to [0;1]
    o_normal = (vec3_to_octahedral_vec2(v_normal) + vec2(1.0)) / 2.0;
}
