#pragma once

#include <lin_maths.h>

#include <cstdint>

namespace hrz::text
{

// Size in pixels of the side of the square region
// allocated to each glyph in the font texture.
constexpr unsigned int GLYPH_SLOT_SIZE = 40;

// Margin around each glyph SDF, to avoid errors due to
// linear texture filtering using values from neighbours.
// (In practice the SDF extends inside the margin, because
// it can be computed and its the most correct thing to do.)
constexpr unsigned int SDF_MARGIN = 2;

// Size of the usable SDF for each glyph.
constexpr unsigned int SDF_SIZE = GLYPH_SLOT_SIZE - SDF_MARGIN * 2;

// Defines the size of the region around each glyph where
// the SDF extends. This is used to display the outline.
constexpr unsigned int SDF_PADDING = 4;

// Actual size of the glyph in the SDF.
constexpr unsigned int GLYPH_SIZE = SDF_SIZE - SDF_PADDING * 2;

constexpr unsigned int GLYPHS_PER_ROW = 50;
constexpr uint32_t TEXTURE_SIZE = GLYPH_SLOT_SIZE * GLYPHS_PER_ROW;
constexpr unsigned int MAX_GLYPHS_PER_FONT = GLYPHS_PER_ROW * GLYPHS_PER_ROW;

struct Glyph
{
    uint32_t in_font_index;
    lm::vec2 offset;
    float pixel_scale;
    uint32_t in_texture_index;
};

} // namespace hrz::text
