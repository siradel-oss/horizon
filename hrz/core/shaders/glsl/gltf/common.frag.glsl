#pragma once

#include "common/blend_modes.glsl"
#include "common/palette.glsl"

#include "gltf/alpha.frag.glsl"

#ifndef GLTF_IMPOSTOR
#include "common/logz.glsl"
#include "common/clip.frag.glsl"
#include "common/sun_lighting.frag.glsl"
#include "common/viewshed.frag.glsl"
#include "common/highlight.glsl"
#endif

#define PALETTE_FN_NAME apply_material_palette
#define PALETTE_ADDITIONAL_ARGUMENTS int index,
#define PALETTE_UBO_PATH hrz_mesh.materials[index]
#include "common/apply_palette.inl.glsl"
#undef PALETTE_FN_NAME
#undef PALETTE_ADDITIONAL_ARGUMENTS
#undef PALETTE_UBO_PATH

uniform sampler2D hrz_material_texture_0;
uniform sampler2D hrz_material_texture_1;

#ifndef GLTF_IMPOSTOR
void handle_depth_and_clip()
{
    test_clip();
#ifndef GLTF_DEPTH
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
#endif
}
#endif

vec4 compute_texture_color(int material, sampler2D material_texture, in vec2 uv, out float value)
{
    vec4 tex_color = texture(material_texture, uv);

    if (hrz_prim.materials[material].use_data_texture)
    {
        value = tex_color.r;
        tex_color = apply_material_palette(material, tex_color.r);
    }

    return tex_color;
}

vec4 compute_material_color_lin(int material, in vec4 tex_color)
{
    vec4 color_lin = srgb_to_linear(tex_color) * hrz_prim.materials[material].material_color;
    color_lin.a = handle_material_alpha_mode(hrz_prim.materials[material].alpha_mode, hrz_prim.materials[material].alpha_cutoff, color_lin.a);
    return color_lin;
}

vec4 compute_material_color_lin(uint blend_mode, float blend_strength, out float value)
{
    bool apply_feature_color_to_overlay = hrz_mesh.geometry.apply_feature_color_to_overlay;

    value = 0.0;

    vec4 color_lin = v_geometry_color_lin;

    vec4 base_texture_color = compute_texture_color(0, hrz_material_texture_0, v_uv_0, value);
    color_lin *= compute_material_color_lin(0, base_texture_color);

    handle_alpha_discard(color_lin.a);

    if (!apply_feature_color_to_overlay)
    {
        color_lin = blend_linear(blend_mode, color_lin, v_feature_color_lin.rgb, blend_strength);
        color_lin.a *= v_feature_color_lin.a;
    }

    if (hrz_mesh.geometry.overlay_material_enabled)
    {
        vec4 overlay_texture_color = compute_texture_color(1, hrz_material_texture_1, v_uv_1, value);
        vec4 overlay_color_lin = compute_material_color_lin(1, overlay_texture_color);
        overlay_color_lin.a *= hrz_mesh.geometry.overlay_material_opacity;

        // Blending with premultiplication
        color_lin.rgb = color_lin.rgb * color_lin.a * (1.0 - overlay_color_lin.a) + overlay_color_lin.rgb * overlay_color_lin.a;
        color_lin.a = overlay_color_lin.a + color_lin.a * (1.0 - overlay_color_lin.a);

        // Undo premultiplication because the rest of the pipeline doesn't use it.
        if (color_lin.a > 0.0)
        {
            color_lin.rgb /= color_lin.a;
        }
    }

    if (apply_feature_color_to_overlay)
    {
        color_lin = blend_linear(blend_mode, color_lin, v_feature_color_lin.rgb, blend_strength);
        color_lin.a *= v_feature_color_lin.a;
    }

    // Convert the alpha from sRGB's gamma.
    color_lin.a = pow(color_lin.a, 2.2);
    color_lin.rgb *= color_lin.a;

    return color_lin;
}

#ifdef GLTF_VISUAL
vec3 get_normal()
{
    if (hrz_mesh.geometry.flat_shaded)
    {
        vec3 tangent = dFdx(v_view_pos);
        vec3 bitangent = dFdy(v_view_pos);
        return normalize(cross(tangent, bitangent));
    }
    else
    {
        if (gl_FrontFacing) return v_normal;
        else return -v_normal;
    }
}
#endif

#if defined(GLTF_VISUAL) && !defined(GLTF_IMPOSTOR)
vec4 apply_color_decoration(vec4 color_lin, vec3 normal, uvec3 feature_reference)
{
    if (hrz_prim.geometry.lighting_enabled)
    {
        color_lin.rgb *= do_sun_lighting(normal, hrz_frame.view_sun_direction, v_altitude, v_normal_to_ground, hrz_prim.geometry.receive_shadows);
    }

    vec4 color = linear_to_srgb(color_lin);
    // Convert the alpha to sRGB's gamma.
    color.a = pow(color.a, 1.0 / 2.2);

    color = compute_viewshed_color(color, normal);
    color = mix_premultiplied_colors(color, compute_clip_outline_color());

    if (hrz_frame.quick_highlight_feature_reference == feature_reference)
    {
        color = apply_quick_highlight_color(color);
    }

    return color;
}
#endif
