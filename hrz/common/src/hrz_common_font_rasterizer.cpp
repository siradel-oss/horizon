#include "hrz_common_font_rasterizer.h"

#include <hrz_common_profiling.h>
#include <hrz_common_woff.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_thread.h>

#include <msdf.h>

#include <atomic>
#include <cassert>
#include <mutex>

namespace hrz
{
namespace font_rasterizer
{
namespace
{
struct ParsedFont
{
    std::variant<std::span<const std::byte>, blobs::BlobData> raw_data;
    stbtt_fontinfo stbtt_font;
    hb_font_t* hb_font;
};

struct RasterizedFont
{
    using GlyphMap = hrz::flat_hash_map<uint32_t, Glyph>;

    std::variant<std::span<const std::byte>, blobs::BlobHandle> raw_data;
    FontInfo info;

    // In order to allow retrieving info on an already rasterised glyph (which
    // is the most common operation) without locking the mutex, the glyph map
    // is double-buffered. `glyphs` always points to the front one (which can
    // be read from, but not written to). The mutex is only locked when adding
    // a new glyph.
    // Index is in-font index.
    GlyphMap glyph_map_0;
    GlyphMap glyph_map_1;
    std::atomic<GlyphMap*> glyphs;

    uint32_t rasterized_glyph_count = 0;

    // The atomic boolean allows the main thread not to lock the mutex when
    // checking for newly rasterised glyphs when there are none.
    std::vector<RasterizedGlyph> new_glyphs;
    std::atomic<bool> has_new_glyphs;

    std::mutex mutex;
};
} // namespace
} // namespace font_rasterizer

struct FontRasterizer
{
    using FontIndexPool = hrz::GenIndexPool<font_rasterizer::FontHandle, 32, 32>;
    using FontPool = hrz::GenObjectPool<font_rasterizer::RasterizedFont, FontIndexPool, 64>;

    FontPool fonts;
};

namespace font_rasterizer
{
Font::Font(Font&& other) noexcept
{
    font_handle = other.font_handle;
    raw_data = std::move(other.raw_data);
    stbtt_font = other.stbtt_font;
    hb_font = other.hb_font;
    info = other.info;

    other.font_handle = 0;
    other.hb_font = nullptr;
}

Font& Font::operator=(Font&& other) noexcept
{
    if (&other != this)
    {
        font_handle = other.font_handle;
        raw_data = std::move(other.raw_data);
        stbtt_font = other.stbtt_font;
        hb_font = other.hb_font;
        info = other.info;

        other.font_handle = 0;
        other.hb_font = nullptr;
    }
    return *this;
}

namespace
{

std::optional<ParsedFont> parse_font(
    BlobAllocator* ba,
    std::variant<std::span<const std::byte>, blobs::BlobHandle>& raw_data)
{
    HRZ_SCOPED_SAMPLE("parse font");

    ParsedFont parsed_font;

    const std::byte* font_data = nullptr;
    size_t font_data_size = 0;

    if (std::holds_alternative<blobs::BlobHandle>(raw_data))
    {
        parsed_font.raw_data = {std::get<blobs::BlobHandle>(raw_data).get_data()};
        font_data = std::get<blobs::BlobData>(parsed_font.raw_data).data();
        font_data_size = std::get<blobs::BlobData>(parsed_font.raw_data).size();
    }
    else
    {
        parsed_font.raw_data = {std::get<std::span<const std::byte>>(raw_data)};
        font_data = std::get<std::span<const std::byte>>(parsed_font.raw_data).data();
        font_data_size = std::get<std::span<const std::byte>>(parsed_font.raw_data).size();
    }

    if (hrz::is_woff_or_woff2(std::span{font_data, font_data_size}))
    {
        auto blob_opt = hrz::woff_or_woff2_to_ttf(ba, std::span{font_data, font_data_size});
        if (!blob_opt.has_value())
        {
            HRZ_LOG_ERROR("Could not convert WOFF/2 to TTF");
            return std::nullopt;
        }

        auto blob = blob_opt.value();
        raw_data = blob;
        parsed_font.raw_data = blob.get_data();
        font_data = std::get<blobs::BlobData>(parsed_font.raw_data).data();
        font_data_size = std::get<blobs::BlobData>(parsed_font.raw_data).size();
    }

    int font_count = stbtt_GetNumberOfFonts((const unsigned char*)font_data);
    if (font_count < 1)
    {
        HRZ_LOG_ERROR("No font found in data blob");
        return std::nullopt;
    }

    int font_offset = stbtt_GetFontOffsetForIndex((const unsigned char*)font_data, 0);

    if (!stbtt_InitFont(&parsed_font.stbtt_font, (const unsigned char*)font_data, font_offset))
    {
        HRZ_LOG_ERROR("Could not parse font data blob");
        return std::nullopt;
    }

    hb_blob_t* blob;
    hb_face_t* face;

    blob = hb_blob_create(
        (const char*)font_data, (unsigned int)font_data_size, HB_MEMORY_MODE_READONLY, nullptr,
        nullptr);
    if (!blob)
    {
        HRZ_LOG_ERROR("Font blob create failed");
        return std::nullopt;
    }

    face = hb_face_create(blob, 0);
    if (!face)
    {
        HRZ_LOG_ERROR("Font face create failed");
        return std::nullopt;
    }

    hb_blob_destroy(blob); // face keeps its ref

    parsed_font.hb_font = hb_font_create(face);
    if (!parsed_font.hb_font)
    {
        HRZ_LOG_ERROR("Font create failed");
        return std::nullopt;
    }

    hb_face_destroy(face); // font keeps ref

    return std::optional<ParsedFont>{std::move(parsed_font)};
}

FontInfo read_font_info(const ParsedFont& font)
{
    // Ascent and descent of the font are the highest and lowest y coordinates
    // of all glyphs in the font.
    // Line gap is added between two successive lines.
    int ascent;
    int descent;
    int line_gap;
    stbtt_GetFontVMetrics(&font.stbtt_font, &ascent, &descent, &line_gap);

    // 1 em isn't necessarily equal to the distance between ascent and descent.
    float font_height_to_em = stbtt_ScaleForMappingEmToPixels(&font.stbtt_font, ascent - descent);

    int typo_ascent;
    int typo_descent;
    int typo_line_gap;
    if (stbtt_GetFontVMetricsOS2(&font.stbtt_font, &typo_ascent, &typo_descent, &typo_line_gap)
        == 1)
    {
        // The font has typographic vertical metrics, use them instead.
        ascent = typo_ascent;
        descent = typo_descent;
        line_gap = typo_line_gap;
    }

    FontInfo info;
    info.internal_units_to_em = font_height_to_em / (ascent - descent);
    info.ascent = ascent * info.internal_units_to_em;
    info.descent = descent * info.internal_units_to_em;
    info.line_gap = line_gap * info.internal_units_to_em;

    return info;
}

// The `info.in_texture_index` field of the returned value is unset.
RasterizedGlyph rasterize_glyph(const Font& font, unsigned int in_font_index)
{
    HRZ_SCOPED_SAMPLE("rasterize new glyph");

    RasterizedGlyph glyph;
    glyph.info.is_blank = true;
    glyph.info.in_font_index = in_font_index;
    glyph.info.in_texture_index = std::numeric_limits<unsigned int>::max();
    glyph.info.offset = lm::vec2(0, 0);

    // We target a glyph SDF scale that fits nicely into the slot according to the
    // font metrics. This helps with having enough padding for the outline.
    // The actual scale may be different if the character is smaller or larger.
    float target_scale = font.info.internal_units_to_em * GLYPH_SIZE;

    ex_metrics_t metrics;
    lm::vec3* float_raster = (lm::vec3*)ex_msdf_glyph(
        &font.stbtt_font, in_font_index, target_scale, GLYPH_SLOT_SIZE, GLYPH_SLOT_SIZE, SDF_MARGIN,
        SDF_PADDING, (int)true, &metrics);

    // Convert from the target scale to the actual scale. These scales convert
    // from the font's internal unit to SDF pixels.
    // target_scale = font.info.internal_to_em * GLYPH_SIZE
    // actual_scale = metrics.scale
    // scale_ratio = target_scale / actual_scale
    //             = (font.info.internal_to_em * GLYPH_SIZE) / metrics.scale
    // glyph sdf_pixel_to_em = scale_ratio / GLYPH_SIZE
    // After simplifying we have:
    glyph.info.sdf_pixel_to_em = font.info.internal_units_to_em / metrics.scale;

    //                   ┌────────────────────────────────────────────────────────┐
    //                   │             :    :                  :                  │
    //        ascent ····│························································│
    //                   │             :    :                  :                  │
    //                   │             :    :                  :                  │
    //           iy1 ····│························xxxxxxxxxxx·····················│
    //                   │             :    :   xx           xx:                  │
    //                   │             :    : xx               x                  │
    //                   │             :    :x                 :                  │
    //                   │             :    x                  :                  │
    //                   │             :    x                  :                  │
    //                   │             :    x                  :                  │
    //                   │             :    x                  :                  │
    //                   │             :    x                  :                  │
    //                   │             :    x                  :                  │
    //                   │             :    x                  :                  │
    //                   │             :    xx                 x                  │
    //                   │             :    : xx              x:                  │
    //                   │             :    :   xx        xxxx :                  │
    // baseline == 0 ····│·············@··········xxxxxxxx························│
    //                   │             :    :        x         :                  │
    //                   │             :    :        xxxxx     :                  │
    //                   │             :    :             x    :                  │
    //                   │             :    :             x    :                  │
    //           iy0 ····│·························xxxxxxx     :                  │
    //       descent ····│························································│
    //                   │             :    :                  :                  │
    //                   │             :    :                  :                  │
    //                   │             :    :                  :                  │
    //                   └────────────────────────────────────────────────────────┘
    //                                 :    :                  :
    //                                 :    :                  :
    //                                 :    :                  :
    //                       left_bearing  ix0                ix1
    //
    // ix0, ix1, iy0, iy1 from the metrics are in the font's internal units.
    // left_bearing from metrics is in SDF pixels. (The returned metrics
    // from ex_msdf_glyph() are not very consistent.)
    // Ascent and descent are in em. They are the highest and lowest glyphs
    // can go in the font.
    // descent is negative for glyphs that reach below the baseline.
    // left_bearing can be negative, in which case it is to the right of ix0.
    //
    // The origin of the glyph, or its pen position, is on the baseline and to
    // the left of the glyph (i.e. at coordinates (i0 - left_bearing, 0) in
    // internal font units). It is marked @ in the diagram. All shaping posi-
    // tions (what comes out of Harfbuzz) is relative to this position.
    //
    // The glyph is centered in its SDF, but we want to provide a way to place
    // the glyph according to its origin. The offset allows translating from
    // the top-left corner of the SDF to the origin.
    float offset_x_px =
        (SDF_SIZE * 0.5f - (metrics.ix1 - metrics.ix0) * metrics.scale * 0.5f
         - metrics.left_bearing);
    float offset_y_px =
        ((SDF_SIZE * 0.5f) + (metrics.iy1 - metrics.iy0) * metrics.scale * 0.5f
         + metrics.iy0 * metrics.scale);

    // Convert the offset to em and negate it, so that it can be simply added
    // to glyph positions when compositing text.
    glyph.info.offset = -lm::vec2(offset_x_px, offset_y_px) * glyph.info.sdf_pixel_to_em;

    if (float_raster == nullptr)
    {
        // The glyph has no graphical representation, i.e. it's blank.
        return glyph;
    }

    // Some SDFs are inside-out, I don't know why.
    // The first pixel is always outside the glyph, so by checking
    // its value we can know whether the SDF is inverted or not.
    //      -tpetillon, 2020-08-20
    lm::vec3 first_pixel = float_raster[0];
    float first_value = std::max(
        std::min(first_pixel.r, first_pixel.g),
        std::min(std::max(first_pixel.r, first_pixel.g), first_pixel.b));
    bool is_inverted = first_value > 0.5f;

    for (unsigned int y = 0; y < GLYPH_SLOT_SIZE; ++y)
    {
        for (unsigned int x = 0; x < GLYPH_SLOT_SIZE; ++x)
        {
            auto to_byte = [&](float f)
            {
                f = is_inverted ? 0.5f - f : f;
                return (uint8_t)std::round(
                    hrz::clamp((f / GLYPH_SLOT_SIZE) * 255.0f + 127.0f, 0.0f, 255.0f));
            };

            lm::vec3 f = float_raster[y * GLYPH_SLOT_SIZE + x];
            glyph.raster[y * GLYPH_SLOT_SIZE + x] = {to_byte(f.x), to_byte(f.y), to_byte(f.z)};
        }
    }

    free(float_raster);

    glyph.info.is_blank = false;

    return glyph;
}
} // namespace

FontRasterizer* create()
{
    return new FontRasterizer();
}

void destroy(FontRasterizer* rasterizer)
{
    assert(rasterizer);
    delete rasterizer;
}

std::optional<FontHandle> add_font(
    FontRasterizer* rasterizer,
    BlobAllocator* ba,
    std::span<const std::byte> raw_data_in)
{
    assert(rasterizer);

    std::variant<std::span<const std::byte>, blobs::BlobHandle> raw_data = raw_data_in;
    auto parsed_font_opt = parse_font(ba, raw_data);
    if (!parsed_font_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not parse font");
        return std::nullopt;
    }

    auto handle = rasterizer->fonts.alloc();
    auto font = rasterizer->fonts.get_object(handle);

    font->raw_data = std::move(raw_data);
    font->info = read_font_info(parsed_font_opt.value());
    font->glyphs = &font->glyph_map_0;
    font->has_new_glyphs = false;

    return handle;
}

std::optional<FontHandle> add_font(
    FontRasterizer* rasterizer,
    BlobAllocator* ba,
    blobs::BlobHandle blob_in)
{
    assert(rasterizer);

    std::variant<std::span<const std::byte>, blobs::BlobHandle> raw_data = blob_in;
    auto parsed_font_opt = parse_font(ba, raw_data);
    if (!parsed_font_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not parse font");
        return std::nullopt;
    }

    auto handle = rasterizer->fonts.alloc();
    auto font = rasterizer->fonts.get_object(handle);

    font->raw_data = std::move(raw_data);
    font->info = read_font_info(parsed_font_opt.value());
    font->glyphs = &font->glyph_map_0;
    font->has_new_glyphs = false;

    return handle;
}

void remove_font(FontRasterizer* rasterizer, FontHandle handle)
{
    assert(rasterizer);

    rasterizer->fonts.release(handle);
}

FontInfo get_font_info(FontRasterizer* rasterizer, FontHandle handle)
{
    assert(rasterizer);

    auto font = rasterizer->fonts.get_object(handle);

    if (font == nullptr)
    {
        HRZ_LOG_ERROR("Unknown font handle: {}", handle);
        return {};
    }

    return font->info;
}

Font get_font(FontRasterizer* rasterizer, FontHandle handle)
{
    assert(rasterizer);

    auto font = rasterizer->fonts.get_object(handle);

    if (font == nullptr)
    {
        HRZ_LOG_ERROR("Unknown font handle: {}", handle);
        return {};
    }

    // The font data is parsed each time a font is obtained through this function.
    // This is because parsing a font creates some data structures that refer to
    // the raw data through pointers. But the raw data can be stored in a blob.
    // We don't want to prevent the blob from being relocated if the blob allocator
    // needs to compact its memory, especially because fonts tend to remain loaded
    // in memory for a long time. So instead the solution is to only lock the blob
    // in place and parse the font each time it is used to bake texts.
    // Fortunately parsing a font only involves reading its header and lasts only
    // for a few microseconds.
    // We give nullptr as blob allocator, because this shouldn't allocate new memory.
    // If it does, it will assert and we will detect and fix it.
    auto parsed_font = parse_font(nullptr, font->raw_data);

    return {
        handle, std::move(parsed_font->raw_data), parsed_font->stbtt_font, parsed_font->hb_font,
        font->info};
}

Glyph get_glyph_info(
    FontRasterizer* rasterizer,
    const Font& parsed_font,
    unsigned int in_font_index)
{
    assert(rasterizer);

    Glyph empty_glyph;
    empty_glyph.is_blank = true;
    empty_glyph.in_font_index = in_font_index;
    empty_glyph.in_texture_index = std::numeric_limits<unsigned int>::max();
    empty_glyph.sdf_pixel_to_em = 0;
    empty_glyph.offset = lm::vec2(0, 0);

    auto font = rasterizer->fonts.get_object(parsed_font.font_handle);

    if (font == nullptr)
    {
        HRZ_LOG_ERROR("Unknown font handle: {}", parsed_font.font_handle);
        return empty_glyph;
    }

    {
        auto glyphs = font->glyphs.load(std::memory_order_acquire);

        auto it = glyphs->find(in_font_index);
        if (it != glyphs->end())
        {
            return it->second;
        }
    }

    auto new_glyph = rasterize_glyph(parsed_font, in_font_index);

    HRZ_SCOPED_LOCK(font->mutex);

    // The glyph may have been added by another thread during rasterisation,
    // so we must check for its presence again.

    auto front_glyph_map = font->glyphs.load(std::memory_order_acquire);

    auto it = front_glyph_map->find(in_font_index);
    if (it != front_glyph_map->end())
    {
        return it->second;
    }

    if (front_glyph_map->size() >= MAX_GLYPHS_PER_FONT)
    {
        HRZ_LOG_WARNING(
            "Cannot rasterize glyph at index {}: No more space available", in_font_index);
        return empty_glyph;
    }

    if (!new_glyph.info.is_blank)
    {
        new_glyph.info.in_texture_index = font->rasterized_glyph_count;
        font->rasterized_glyph_count += 1;
    }

    auto back_glyph_map =
        front_glyph_map == &font->glyph_map_0 ? &font->glyph_map_1 : &font->glyph_map_0;

    back_glyph_map->insert({in_font_index, new_glyph.info});
    std::swap(front_glyph_map, back_glyph_map);
    font->glyphs.store(front_glyph_map, std::memory_order_release);
    back_glyph_map->insert({in_font_index, new_glyph.info});

    font->new_glyphs.push_back(new_glyph);
    font->has_new_glyphs.store(true, std::memory_order_release);

    return new_glyph.info;
}

std::optional<std::vector<RasterizedGlyph>> get_new_glyphs(
    FontRasterizer* rasterizer,
    FontHandle handle)
{
    assert(rasterizer);

    auto font = rasterizer->fonts.get_object(handle);

    if (font == nullptr)
    {
        HRZ_LOG_ERROR("Unknown font handle: {}", handle);
        return std::nullopt;
    }

    if (!font->has_new_glyphs.load(std::memory_order_acquire))
    {
        return std::nullopt;
    }

    HRZ_SCOPED_LOCK(font->mutex);

    assert(!font->new_glyphs.empty());

    std::vector<RasterizedGlyph> new_glyphs{std::move(font->new_glyphs)};
    font->new_glyphs = {};
    font->has_new_glyphs.store(false, std::memory_order_release);

    return {std::move(new_glyphs)};
}
} // namespace font_rasterizer
} // namespace hrz
