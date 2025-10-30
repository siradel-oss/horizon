#pragma once

#include "hrz_common_blob_allocator.h"
#include "hrz_common_shader_defines.h"
#include "hrz_common_text.h"

#include <hrz_fnd_variant.h>

#include <hb.h>
#include <stb_truetype.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hrz
{
struct FontRasterizer;

namespace font_rasterizer
{
// Size in pixels of the side of the square region
// allocated to each glyph in the font texture.
constexpr unsigned int GLYPH_SLOT_SIZE = 44;

// Margin around each glyph SDF, to avoid errors due to
// linear texture filtering using values from neighbours.
// (In practice the SDF extends inside the margin, because
// it can be computed and its the most correct thing to do.)
constexpr unsigned int SDF_MARGIN = 2;

// Size of the usable SDF for each glyph.
constexpr unsigned int SDF_SIZE = GLYPH_SLOT_SIZE - SDF_MARGIN * 2;

// Defines the size of the region around each glyph where
// the SDF extends. This is used to display the outline.
constexpr unsigned int SDF_PADDING = HRZ_S_TEXT_SDF_PADDING;

// Actual size of the glyph in the SDF.
// The maximum padding in ems is derived from the ratio between
// GLYPH_SIZE and SDF_SIZE. If they are changed, the documentation
// must be updated.
constexpr unsigned int GLYPH_SIZE = SDF_SIZE - SDF_PADDING * 2;

constexpr unsigned int GLYPHS_PER_ROW = 46;
constexpr uint32_t TEXTURE_SIZE = GLYPH_SLOT_SIZE * GLYPHS_PER_ROW;
static_assert(TEXTURE_SIZE == HRZ_S_TEXT_TEXTURE_SIZE);
constexpr unsigned int MAX_GLYPHS_PER_FONT = GLYPHS_PER_ROW * GLYPHS_PER_ROW;

using FontHandle = uint64_t;

struct FontInfo
{
    // Converts from the font's internal size to em.
    // Useful when working with Harfbuzz's shaping output.
    float internal_units_to_em;

    float ascent;   // in em
    float descent;  // in em, negative if below the baseline
    float line_gap; // in em

    // Height of a single line, or the first line in a paragraph.
    // in em
    float single_line_height() const { return ascent - descent; }

    // Height between two successive baselines in a paragraph.
    // in em
    float new_line_height() const { return -descent + line_gap + ascent; }
};

struct Font
{
    // We have no choice but to use a BlobData instead of a BlobHandle, because
    // stbtt_fontinfo refers directly to the ttf data, so it needs to be pinned in memory.
    // I guess we could just use a std::unique_ptr<std::byte[]>, which would avoid having
    // a pinned blob forever.
    // @Todo Investigate font memory being pinned.
    FontHandle font_handle;
    std::variant<std::span<const std::byte>, blobs::BlobData> raw_data;
    stbtt_fontinfo stbtt_font;
    hb_font_t* hb_font = nullptr;
    FontInfo info;

    Font() = default;

    Font(
        FontHandle font_handle,
        std::variant<std::span<const std::byte>, blobs::BlobData> raw_data,
        const stbtt_fontinfo& stbtt_font,
        hb_font_t* hb_font,
        const FontInfo& info) :
        font_handle(font_handle),
        raw_data(std::move(raw_data)),
        stbtt_font(stbtt_font),
        hb_font(hb_font),
        info(info)
    {
    }

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;

    ~Font()
    {
        if (hb_font != nullptr)
        {
            hb_font_destroy(hb_font);
        }
    }
};

struct Glyph
{
    // Some glyphs have no graphical representation, typically whitespace.
    bool is_blank;

    uint32_t in_font_index;
    uint32_t in_texture_index;

    // Converts from the size of one SDF pixel to em.
    float sdf_pixel_to_em;

    // Offset in em from the top-left corner of the SDF to the origin of the
    // character (on the baseline, on the left of the character).
    lm::vec2 offset;

    float get_sdf_size_in_em() const { return SDF_SIZE * sdf_pixel_to_em; }

    lm::bbox2 get_uv() const
    {
        constexpr float margin = SDF_MARGIN / (float)TEXTURE_SIZE;
        constexpr unsigned int glyphs_per_row = TEXTURE_SIZE / GLYPH_SLOT_SIZE;

        float u0 = (in_texture_index % glyphs_per_row) / (float)glyphs_per_row + margin;
        float u1 = u0 + SDF_SIZE / (float)TEXTURE_SIZE;
        float v0 = (in_texture_index / glyphs_per_row) / (float)glyphs_per_row + margin;
        float v1 = v0 + SDF_SIZE / (float)TEXTURE_SIZE;

        return lm::bbox2{{u0, v0}, {u1, v1}};
    }
};

// The raster y axis points downward.
struct RasterizedGlyph
{
    static constexpr size_t RASTER_SIZE = GLYPH_SLOT_SIZE * GLYPH_SLOT_SIZE;

    Glyph info;
    lm::ubvec3 raster[RASTER_SIZE];

    std::span<lm::ubvec3> raster_span() { return {raster, RASTER_SIZE}; }

    std::span<const lm::ubvec3> raster_span() const { return {raster, RASTER_SIZE}; }
};

FontRasterizer* create();

void destroy(FontRasterizer*);

std::optional<FontHandle> add_font(FontRasterizer*, BlobAllocator* ba, std::span<const std::byte>);
std::optional<FontHandle> add_font(FontRasterizer*, BlobAllocator* ba, blobs::BlobHandle);

void remove_font(FontRasterizer*, FontHandle);

FontInfo get_font_info(FontRasterizer*, FontHandle);

// Don't hold the font object any longer than necessary, as it prevents the
// blob that contains the font's data from being relocated.
Font get_font(FontRasterizer*, FontHandle);

Glyph get_glyph_info(FontRasterizer*, const Font&, unsigned int in_font_index);

std::optional<std::vector<RasterizedGlyph>> get_new_glyphs(FontRasterizer*, FontHandle);
} // namespace font_rasterizer
} // namespace hrz
