// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

namespace hrz
{

// Type of rendering. For instance "Visual" is the normal mode.
// This is used to switch shader in renderable objects.
enum RenderType
{
    RenderVisual,
    RenderPicking,
    RenderPlanetFeedback,
    RenderDepth,
    RenderDecal,
    RenderShadows,
    RenderViewshed,
    RenderSelection,
};

// Bin numbers determine the render order, inside a render pass.
enum RenderBinBit
{
    RenderWorldTransparentBinBit,
    RenderWorldOpaqueBinBit,
    RenderDecalBinBit,
    RenderSymbolicBinBit,
    RenderSymbolicOverlayBinBit,
    RenderPlanetBinBit,
    RenderHeatmapBinBit,
    RenderFlatOverlayBinBit,
    RenderInWorldUiBinBit,
    RenderUiBinBit,
};

// An object can belong to multiple bins, but they may be drawn multiple times.
// A render pass selects the objects it will render based on its bin.
enum RenderBin
{
    RenderWorldOpaqueBin = 1ULL << RenderWorldOpaqueBinBit,
    RenderWorldTransparentBin = 1ULL << RenderWorldTransparentBinBit,
    RenderDecalBin = 1ULL << RenderDecalBinBit,
    RenderSymbolicBin = 1ULL << RenderSymbolicBinBit,
    RenderSymbolicOverlayBin = 1ULL << RenderSymbolicOverlayBinBit,
    RenderPlanetBin = 1ULL << RenderPlanetBinBit,
    RenderHeatmapBin = 1ULL << RenderHeatmapBinBit,
    RenderFlatOverlayBin = 1ULL << RenderFlatOverlayBinBit,
    RenderInWorldBin = 1ULL << RenderInWorldUiBinBit,
    RenderUiBin = 1ULL << RenderUiBinBit,

    RenderAllWorldBins = RenderWorldOpaqueBin | RenderWorldTransparentBin | RenderPlanetBin,
    RenderAllPhysicalBins = RenderAllWorldBins | RenderDecalBin | RenderSymbolicBin
        | RenderSymbolicOverlayBin | RenderInWorldBin | RenderUiBin,
};

} // namespace hrz
