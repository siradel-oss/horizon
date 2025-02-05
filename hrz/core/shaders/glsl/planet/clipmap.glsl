#pragma once

#include "planet/defs.glsl"

const uint PYRAMID_COUNT = uint(log2(float(CLIPMAP_SIZE))) + 1u;

#define CLIPMAP_LEVEL_SIZE(LOD) (min(1u << LOD, uint(CLIPMAP_SIZE)))

vec2 compute_diff_wrapping_x(vec2 base_a, vec2 partial_a, vec2 base_b, vec2 partial_b)
{
    const float full = pow(2.0, float(MAX_LOD)) * float(MERCATOR_TILE_SIZE);

    vec2 diff;

    float base_diff_x = base_a.x - base_b.x;
    float base_diff_x_pos = base_diff_x + full;
    float base_diff_x_neg = base_diff_x - full;

    float partial_diff_x = partial_a.x - partial_b.x;

    float diff_x = base_diff_x + partial_diff_x;
    float diff_x_pos = base_diff_x_pos + partial_diff_x;
    float diff_x_neg = base_diff_x_neg + partial_diff_x;

    if (abs(base_diff_x) > abs(base_diff_x_pos) || abs(diff_x) > abs(diff_x_pos))
    {
        diff.x = diff_x_pos;
    }
    else if (abs(base_diff_x) > abs(base_diff_x_neg) || abs(diff_x) > abs(diff_x_neg))
    {
        diff.x = diff_x_neg;
    }
    else
    {
        diff.x = diff_x;
    }

    float base_diff_y = base_a.y - base_b.y;
    float partial_diff_y = partial_a.y - partial_b.y;

#if defined(WORKAROUND_007)
    // @Workaround(007-Apple-ArithmeticPrecisionLoss)
    // The execution flow cannot enter the first two blocks. They are only here to prevent
    // loss of precision when computing the y-component on macOS and iOS, due to what codegen
    // does on these platforms.
    // No amount of `highp` qualifiers fixes this.
    // `diff` was previously calculated with:
    //     vec2 diff = (base_a - base_b) + (partial_a - partial_b);
    // The x value was correct, but only thanks to the presence of the conditions that could
    // affect its value later-on, even when they didn't actually touch the value.
    if (base_diff_y > full)
    {
        diff.y = full;
    }
    else if (partial_diff_y < -full)
    {
        diff.y = -full;
    }
    else
#endif
    {
        diff.y = base_diff_y + partial_diff_y;
    }

    return diff;
}

#if defined(WORKAROUND_004)
// @Workaround(004-Safari-UniformBufferArrayLoad)
// We use this to put the centers in register at the beginning of the shaders to accelerate lookup on Safari.
vec4 g_clip_center[LOD_COUNT];
#   define CLIP_CENTER(INDEX) (g_clip_center[(INDEX)])
#else
#   define CLIP_CENTER(INDEX) (hrz_planet.clip_center[(INDEX)])
#endif

uvec3 get_clipmap_slot_for_lod(
    uint required_lod, vec2 base_wmerc_pos, vec2 partial_wmerc_pos)
{
    uint max_lod = max(uint(MAX_LOD) - required_lod, 0u);
    uint tile_size = uint(MERCATOR_TILE_SIZE) << (uint(MAX_LOD) - max_lod);

    // All levels of the clipmap are not necessarily centered around
    // the same point, so for the chosen level we need to check that it
    // lies in the clipmap. Otherwise we go up the clipmap to find a level
    // the point lies in.
    for (uint i = 0u; i < uint(LOD_COUNT); ++i, tile_size *= 2u)
    {
        uint lod = max_lod - i;

        if (lod == 0u) return uvec3(0u);

        uint clip_nb_tiles = CLIPMAP_LEVEL_SIZE(lod);
        float clip_level_size = float(clip_nb_tiles * tile_size);

        // Re-center diff coords on required level
        vec2 level_diff = compute_diff_wrapping_x(
            base_wmerc_pos, partial_wmerc_pos,
            CLIP_CENTER(lod).xy, CLIP_CENTER(lod).zw);

        vec2 in_clip_uv = level_diff / clip_level_size;
        in_clip_uv += vec2(0.5);

        if ((in_clip_uv.x >= 0.0 && in_clip_uv.x < 1.0 &&
             in_clip_uv.y >= 0.0 && in_clip_uv.y < 1.0))
        {
            return uvec3(uvec2(floor(in_clip_uv * float(clip_nb_tiles))), lod);
        }
    }

    return uvec3(0u);
}

void fetch_tile_from_clipmap(
    in highp usampler2DArray indirection, in uvec3 slot, out uvec2 tile_position, out uint lod)
{
    uvec3 pixel = texelFetch(indirection, ivec3(slot), 0).xyz;
    tile_position = pixel.xy;
    lod = pixel.z;
}

void clamp_poles(inout vec2 base, inout vec2 partial)
{
    const float NORTH_LIMIT = pow(2.0, 23.0);
    const float SOUTH_LIMIT = pow(2.0, 31.0) - NORTH_LIMIT;

    if (base.y + partial.y < NORTH_LIMIT)
    {
        base.y = NORTH_LIMIT;
        partial.y = 0.0;
    }

    if (base.y + partial.y > SOUTH_LIMIT)
    {
        base.y = SOUTH_LIMIT;
        partial.y = 0.0;
    }
}

vec2 get_tile_uv(uvec2 tile_position, uint lod, vec2 base_wmerc_pos, vec2 partial_wmerc_pos)
{
    clamp_poles(base_wmerc_pos, partial_wmerc_pos);

    uint tile_size = uint(MERCATOR_TILE_SIZE) << (uint(LOD_COUNT) - lod - 1u);
    uint clip_nb_tiles = CLIPMAP_LEVEL_SIZE(lod);

    float clip_level_size = float(clip_nb_tiles * tile_size);

    // Re-center diff coords on required level
    vec2 level_diff = compute_diff_wrapping_x(base_wmerc_pos, partial_wmerc_pos, CLIP_CENTER(lod).xy, CLIP_CENTER(lod).zw);

    vec2 in_clip_uv = level_diff / clip_level_size;

    // Lod 0 is a bit weird because its center is at the north west corner
    // so we don't want to re-center it and stuff.
    if (lod > 0u)
    {
        in_clip_uv += vec2(0.5);
    }

    return (fract(in_clip_uv * float(clip_nb_tiles)) * float(ATLAS_TILE_SIZE - 2 * ATLAS_TILE_BORDER_SIZE) + float(ATLAS_TILE_BORDER_SIZE)) / float(ATLAS_TILE_SIZE);
}
