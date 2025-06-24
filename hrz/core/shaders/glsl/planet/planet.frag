#include "common/logz.glsl"
#include "common/colors.glsl"
#include "common/ubo_frame.glsl"
#include "common/sun_lighting.frag.glsl"
#include "common/viewshed.frag.glsl"
#include "common/clip.frag.glsl"

#include "planet/defs.glsl"
#include "planet/clipmap.glsl"

#ifndef PLANET_AUX_VIEW
#include "common/flat_overlay_cameras.glsl"

#ifdef PLANET_VISUAL
#define FLAT_OVERLAY_VISUAL
#endif
#ifdef PLANET_PICKING
#define FLAT_OVERLAY_PICKING
#endif
#ifdef PLANET_SELECTION
#define FLAT_OVERLAY_SELECTION
#endif

#include "common/flat_overlay_sampling.glsl"
#endif

#define varying in
#include "planet/interface.glsl"

#define MAX_IMAGERY_GROUP_COUNT HRZ_S_MAX_IMAGERY_GROUP_COUNT

#ifdef PLANET_VISUAL
uniform highp usampler2DArray hrz_imagery_indirection[MAX_IMAGERY_GROUP_COUNT];
uniform lowp sampler2D hrz_imagery_atlas[MAX_IMAGERY_GROUP_COUNT];
#endif

#if defined(PLANET_VISUAL)
layout(location = 0) out vec4 o_color;
#endif

#if defined(PLANET_FEEDBACK)
layout(location = 0) out uvec4 o_feedback;
#endif

#ifdef PLANET_PICKING
layout(location = 0) out highp uvec2 o_object_reference;
layout(location = 1) out highp float o_depth;
#endif

#ifdef PLANET_SELECTION
layout(location = 0) out float o_highlight;
#endif

#if defined(PLANET_VISUAL) || defined(PLANET_FEEDBACK)
float ease_out_cubic(float f)
{
    return 1.0 - (1.0 - f) * (1.0 - f) * (1.0 - f);
}

float ease_flatness(float f)
{
    return (1.0 - f) * (1.0 - ease_out_cubic(f)) + f;
}

float get_lod(float subsample, bool compensate_inclination)
{
    vec2 base_wmerc_pos = v_base_wmerc_pos;
    vec2 partial_wmerc_pos = v_partial_wmerc_pos;
    clamp_poles(base_wmerc_pos, partial_wmerc_pos);

    float dx_flatness = 1.0;
    float dy_flatness = 1.0;

    if (compensate_inclination)
    {
        vec3 a = v_view_pos;
        vec3 a_norm = normalize(a);

        float dx_a_norm_dist = length(dFdx(a_norm));
        float a_dx_a_dist = dx_a_norm_dist * length(a);
        float dx_b_dist = length(dFdx(a));
        dx_flatness = clamp(a_dx_a_dist / dx_b_dist, 0.0, 1.0);

        float dy_a_norm_dist = length(dFdy(a_norm));
        float a_dy_a_dist = dy_a_norm_dist * length(a);
        float dy_b_dist = length(dFdy(a));
        dy_flatness = clamp(a_dy_a_dist / dy_b_dist, 0.0, 1.0);

        dx_flatness = ease_flatness(dx_flatness);
        dy_flatness = ease_flatness(dy_flatness);
    }

    vec2 dx = (dFdx(partial_wmerc_pos) * dx_flatness) / subsample;
    vec2 dy = (dFdy(partial_wmerc_pos) * dy_flatness) / subsample;
    float res = max(dot(dx, dx), dot(dy, dy));
    return max(0.0, 0.5 * log2(res) + 0.5 + hrz_planet.mipmap_bias + log2(hrz_frame.device_pixel_ratio));
}

uvec3 get_clipmap_slot(uint lod)
{
    return get_clipmap_slot_for_lod(lod, v_base_wmerc_pos, v_partial_wmerc_pos);
}

#endif

#ifdef PLANET_VISUAL
void fetch_tile_from_imagery_clipmap(
    in highp usampler2DArray indirection, in uvec3 slot, out uvec2 tile_position, out uint lod)
{
    fetch_tile_from_clipmap(indirection, slot, tile_position, lod);
}

vec2 get_tile_uv(uvec2 tile_position, uint lod)
{
    return get_tile_uv(tile_position, lod, v_base_wmerc_pos, v_partial_wmerc_pos);
}

vec4 fetch_clipmap_color(in lowp sampler2D atlas, in uvec2 tile, in vec2 uv)
{
    vec2 atlas_uv = (vec2(tile) + uv) * float(ATLAS_TILE_SIZE);
    atlas_uv /= vec2(textureSize(atlas, 0).xy);
    return textureLod(atlas, atlas_uv, 0.0);
}

vec4 fetch_clipmap_color_for_slot(highp usampler2DArray indirection, lowp sampler2D atlas, uvec3 slot)
{
    uint lod;
    uvec2 tile_position;
    fetch_tile_from_imagery_clipmap(indirection, slot, tile_position, lod);

    vec2 uv = get_tile_uv(tile_position, lod);

    return fetch_clipmap_color(atlas, tile_position, uv);
}

vec4 sample_group(float lod_blend, uvec3 slot, uvec3 prev_lod_slot, highp usampler2DArray indirection, lowp sampler2D atlas)
{
    if (lod_blend <= 0.0)
    {
        return fetch_clipmap_color_for_slot(indirection, atlas, slot);
    }
    else if (lod_blend >= 1.0)
    {
        return fetch_clipmap_color_for_slot(indirection, atlas, prev_lod_slot);
    }
    else
    {
        vec4 color0 = fetch_clipmap_color_for_slot(indirection, atlas, slot);
        vec4 color1 = fetch_clipmap_color_for_slot(indirection, atlas, prev_lod_slot);
        return mix(color0, color1, lod_blend);
    }
}
#endif

void main()
{
#if defined(WORKAROUND_004)
    // @Workaround(004-Safari-UniformBufferArrayLoad)
    g_clip_center = hrz_planet.clip_center;
#endif

#ifdef PLANET_LOG_DEPTH
    gl_FragDepth = log_depth_value(gl_FragCoord.w);
#endif
    test_clip();

#ifdef PLANET_VISUAL
    float lod_f = max(0.0, get_lod(1.0, hrz_planet.compensate_inclination) - 0.5);
    uint lod_ui = uint(floor(lod_f));
    uvec3 slot = get_clipmap_slot(lod_ui);
    uvec3 prev_lod_slot = get_clipmap_slot(lod_ui + 1u);

    {
        float lod_blend = 0.0;

        if (hrz_planet.mix_lods)
        {
            lod_blend = smoothstep(0.0, 1.0, smoothstep(0.0, 1.0, lod_f - floor(lod_f)));
            lod_blend = (lod_blend - 0.5) * 1.5 + 0.5;
        }

        // The alpha channel is premultiplied in rasters,
        // so the new colour only needs to be multiplied by
        // the additional opacity.

        // Blend top to bottom (we can because premultiplied!).
        // Stop as soon as we're opaque.

        vec4 color = compute_overlay_color(v_overlay_cams_clip_pos);

        // Absolutely magnificient!
        if (color.a < 1.0 && (hrz_frame.merge_groups_bitset & 4u) != 0u)
        {
            vec4 this_color = sample_group(lod_blend, slot, prev_lod_slot, hrz_imagery_indirection[2], hrz_imagery_atlas[2]);
            color = mix_premultiplied_colors(this_color, color);
        }

        if (color.a < 1.0 && (hrz_frame.merge_groups_bitset & 2u) != 0u)
        {
            vec4 this_color = sample_group(lod_blend, slot, prev_lod_slot, hrz_imagery_indirection[1], hrz_imagery_atlas[1]);
            color = mix_premultiplied_colors(this_color, color);
        }

        if (color.a < 1.0 && (hrz_frame.merge_groups_bitset & 1u) != 0u)
        {
            vec4 this_color = sample_group(lod_blend, slot, prev_lod_slot, hrz_imagery_indirection[0], hrz_imagery_atlas[0]);
            color = mix_premultiplied_colors(this_color, color);
        }

        o_color = mix_premultiplied_colors(vec4(hrz_frame.terrain_color_opacity.rgb, 1), color);
    }

    vec3 dx = dFdx(v_view_pos);
    vec3 dy = dFdy(v_view_pos);
    vec3 geometry_normal = normalize(cross(dx, dy));
    vec3 sun = vec3(1.0);

    if (hrz_frame.terrain_lighting_enabled)
    {
        vec3 normal = v_normal_altitude.xyz;
        float altitude = v_normal_altitude.w;
        sun = do_sun_lighting(normal, hrz_frame.view_sun_direction,
            altitude + EARTH_RADIUS, v_view_normal_to_sun, hrz_frame.terrain_receive_shadows);
    }

    vec3 color_linear = srgb_to_linear(o_color.rgb) * sun;
    o_color.rgb = linear_to_srgb(color_linear);
    o_color *= hrz_frame.terrain_color_opacity.a;

    o_color = compute_viewshed_color(o_color, geometry_normal);
    o_color = mix_premultiplied_colors(o_color, compute_clip_outline_color());

    if (o_color.a == 0.0)
    {
        discard;
    }
#endif

#ifdef PLANET_FEEDBACK
    uint lod = uint(floor(get_lod(float(HRZ_S_PLANET_FEEDBACK_SUBSAMPLE), false)));
    uvec3 slot = get_clipmap_slot(lod);

    if (slot.z == 255u)
    {
        o_feedback = uvec4(0u);
    }
    else
    {
        slot.z += 1u;
        o_feedback = uvec4(slot, 0u);
    }
#endif

#ifdef PLANET_PICKING
    uvec2 overlay_color = compute_overlay_object_reference(v_overlay_cams_clip_pos);
    if (overlay_color.r != 0u)
    {
        o_object_reference = overlay_color;
    }
    else
    {
        o_object_reference = hrz_planet.object_reference;
    }

    o_depth = 1.0 / gl_FragCoord.w;
#endif

#ifdef PLANET_SELECTION
    float selection_color = compute_selection_overlay_color(v_overlay_cams_clip_pos);
    if (selection_color != 0.0)
    {
        o_highlight = selection_color;
    }
    else
    {
        discard;
    }
#endif
}
