// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#pragma once

// All of this is shared with GLSL, so use a compatible syntax.

#define HRZ_S_DEV_UI_INSTANCES_WIDTH 128
#define HRZ_S_NEAR 0.01
#define HRZ_S_FAR 20000000
#define HRZ_S_MERCATOR_TILE_SIZE 256
#define HRZ_S_CLIPMAP_SIZE 32
#define HRZ_S_CLIPMAP_LOD_COUNT 24
#define HRZ_S_ATLAS_TILE_SIZE 260
#define HRZ_S_ATLAS_TILE_BORDER_SIZE 2
#define HRZ_S_PLANET_FEEDBACK_SUBSAMPLE 4
#define HRZ_S_PLANET_BIN_SIZE 64
#define HRZ_S_MAX_IMAGERY_GROUP_COUNT 3
#define HRZ_S_MAX_OVERLAY_CASCADES 4
#define HRZ_S_POLYLINE_SIDE_IN 1
#define HRZ_S_POLYLINE_SIDE_OUT 2
#define HRZ_S_EARTH_RADIUS 6378137.0
#define HRZ_S_STRAT_RADIUS 6438137.0
#define HRZ_S_WGS84_AXES_LENGTH_RATIO 0.996647189335
#define HRZ_S_SKY_TRANSMITTANCE_LUT_SIZE 128
#define HRZ_S_SKY_VIEW_SIZE 256
#define HRZ_S_SKY_AERIAL_WIDTH 32
#define HRZ_S_SKY_AERIAL_HEIGHT 16
#define HRZ_S_SKY_AERIAL_DEPTH 32
#define HRZ_S_SKY_AERIAL_MAX_DEPTH 300000
#define HRZ_S_SKY_EXPOSURE 12.0
#define HRZ_S_MAX_SUN_CASCADES 4
#define HRZ_S_SUN_SHADOW_MAP_SIZE 1024
#define HRZ_S_VIEWSHED_CNT 1
#define HRZ_S_VIEWSHED_SHADOW_MAP_SIZE 512
#define HRZ_S_MAX_CLIP_PLANES 8
#define HRZ_S_CAMERA_HEIGHT_FAR_OFFSET 11000.0

// If you change this, also change the documentation for
// the maximum text outline width, in text_symbol_element.md.
// It is computed as SDF_PADDING / GLYPH_SIZE.
#define HRZ_S_TEXT_SDF_PADDING 6
#define HRZ_S_TEXT_TEXTURE_SIZE 2024

// If you change this, also change the documentation for
// HrzProtocol.Material.data_texture_palette and
// HrzProtocol.HeatmapVectorRepr.numeric_palette.
// Also the heatmaps.md and dynamic_materials.md doc pages.
#define HRZ_S_MAX_PALETTE_COLOR_STOPS 32

#define HRZ_S_INSTANCE_GROUP_DATA_TEXTURE_WIDTH 512
#define HRZ_S_VECTOR_REPR_DATA_TEXTURE_WIDTH 512
#define HRZ_S_POINT_CLOUD_DATA_TEXTURE_WIDTH 1024

#define HRZ_S_SELECTION_STORAGE_UINT32_TEXTURE_WIDTH 2048

#define HRZ_S_B3DM_DATA_TEXTURE_WIDTH 2048
