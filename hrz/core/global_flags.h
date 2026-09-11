// SPDX-FileCopyrightText: Copyright 2023 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

namespace hrz
{

#define HRZ_FLAGS                                     \
    HRZ_DEFINE_FLAG(EnableRasterAtlasCompression)     \
    HRZ_DEFINE_FLAG(EnableShadows)                    \
    HRZ_DEFINE_FLAG(EnableAtmosphere)                 \
    HRZ_DEFINE_FLAG(EnabledDepthPeelingForUiElements) \
    HRZ_DEFINE_FLAG(DebugDrawFlatOverlayCascades)     \
    HRZ_DEFINE_FLAG(DebugDrawHeatmapOobSampling)      \
    HRZ_DEFINE_FLAG(DebugDrawVectorTileBounds)        \
    HRZ_DEFINE_FLAG(DebugDrawHorizonOcclusionPoints)  \
    HRZ_DEFINE_FLAG(DebugDrawTerrainPatchBboxes)      \
    HRZ_DEFINE_FLAG(DebugFreeze3DTilesCulling)        \
    HRZ_DEFINE_FLAG(DebugFreezeVectorTilesCulling)    \
    HRZ_DEFINE_FLAG(ForceRender)                      \
    HRZ_DEFINE_FLAG(ForceFlatOverlayRender)           \
    HRZ_DEFINE_FLAG(EnableFlatOverlays)               \
    HRZ_DEFINE_FLAG(EnableTerrain)                    \
    HRZ_DEFINE_FLAG(EnableTerrainAdaptiveResolution)

#define HRZ_DEFINE_FLAG(NAME) NAME,

enum class Flag : int
{
    HRZ_FLAGS _FlagCount,
};

#undef HRZ_DEFINE_FLAG

void set_flag(Flag, bool);
bool get_flag(Flag);

void iterate_flags(const std::function<void(const char*, bool)>& callback);

// Debug values are like flags, but hold a tunable number instead of a boolean.

#define HRZ_VALUES HRZ_DEFINE_VALUE(DebugSymbolHorizonDeclutteringPercent, 2.0F, 0.0F, 10.0F)

#define HRZ_DEFINE_VALUE(NAME, DEFAULT, MIN, MAX) NAME,

enum class Value : int
{
    HRZ_VALUES _ValueCount,
};

#undef HRZ_DEFINE_VALUE

void set_value(Value, float);
float get_value(Value);
float* get_value_ptr(Value);
float get_value_min(Value);
float get_value_max(Value);

} // namespace hrz
