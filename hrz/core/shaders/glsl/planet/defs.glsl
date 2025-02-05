#pragma once

#include "defines.glsl"

#define MERCATOR_TILE_SIZE HRZ_S_MERCATOR_TILE_SIZE
#define CLIPMAP_SIZE HRZ_S_CLIPMAP_SIZE
#define ATLAS_TILE_SIZE HRZ_S_ATLAS_TILE_SIZE
#define ATLAS_TILE_BORDER_SIZE HRZ_S_ATLAS_TILE_BORDER_SIZE
#define CLIP_SIZE HRZ_S_CLIPMAP_SIZE
#define LOD_COUNT HRZ_S_CLIPMAP_LOD_COUNT
#define MAX_LOD (HRZ_S_CLIPMAP_LOD_COUNT - 1)
#define MAX_IMAGERY_GROUP_COUNT HRZ_S_MAX_IMAGERY_GROUP_COUNT

layout(std140) uniform PlanetParams
{
    highp uint picking_combined_id;
    highp uint picking_object_id;

    float mipmap_bias;
    bool compensate_inclination;
    bool mix_lods;

    // xy are xy base, zw are xy partial
    // This is done to save on memory because array stride are
    // rounded up to the size of a vec4.
    vec4 clip_center[LOD_COUNT];

} hrz_planet;
