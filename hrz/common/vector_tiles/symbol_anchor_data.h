// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <lin_maths.h>

namespace hrz::vt
{

static constexpr uint32_t AnchorFlag_Unit_Meters = 0;
static constexpr uint32_t AnchorFlag_Unit_Pixels = 1;
static constexpr uint32_t AnchorFlag_Unit_PixelsRelativeToAnchorDistance = 2;
static constexpr uint32_t AnchorFlag_Unit_PixelsRelativeToCameraHeight = 3;

static constexpr uint32_t AnchorFlag_PosOffsetUnitMask = 0x0000'0003;
static constexpr uint32_t AnchorFlag_PosOffsetUnitShift = 0;

static constexpr uint32_t AnchorFlag_ElementSizeUnitMask = 0x0000'000c;
static constexpr uint32_t AnchorFlag_ElementSizeUnitShift = 2;

// Keep this in sync with the GLSL code
static constexpr uint32_t AnchorFlag_AlignXAxisToScreen = 0x0000'0010;
static constexpr uint32_t AnchorFlag_AlignYAxisToScreen = 0x0000'0020;
static constexpr uint32_t AnchorFlag_KeepUpright = 0x0000'0040;

// These flags are not used on the GPU
static constexpr uint32_t AnchorFlag_CanOverlapOtherSymbols = 0x80000000;
static constexpr uint32_t AnchorFlag_HidesOtherSymbols = 0x40000000;
static constexpr uint32_t AnchorFlag_IsOptional = 0x20000000;

struct AnchorPrototype
{
    // See above.
    uint32_t flags;

    float reference_distance;
    float min_relative_scale;
    float max_relative_scale;
};

struct AnchorSpan
{
    uint32_t first_anchor;
    uint32_t anchor_count;
};

struct AnchorCullingInfo
{
    lm::bbox2 rect;

    lm::vec3 in_tile_position;
    lm::vec3 position_offset;

    lm::vec3 rotation; // XYZ euler angles

    lm::vec3 local_east_axis;
    lm::vec3 local_up_axis;

    float priority{};

    uint32_t anchor_prototype_index{};
};

} // namespace hrz::vt
