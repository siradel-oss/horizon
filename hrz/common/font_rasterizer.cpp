#include "hrz/common/font_rasterizer.h"

#include "hrz/common/profiling.h"
#include "hrz/common/woff.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/maths.h"
#include "hrz/fnd/thread.h"

#include <msdfgen/msdfgen.h>

#include <array>
#include <atomic>
#include <cassert>
#include <shared_mutex>

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
    std::variant<std::span<const std::byte>, blobs::BlobHandle> raw_data;
    FontInfo info;
    hrz::flat_hash_map<uint32_t, Glyph> glyphs;
    uint32_t rasterized_glyph_count = 0;

    // The atomic boolean allows the main thread not to lock the mutex when
    // checking for newly rasterised glyphs when there are none.
    std::vector<RasterizedGlyph> new_glyphs;
    std::atomic<bool> has_new_glyphs;

    std::shared_mutex mutex;
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

msdfgen::Shape stbtt_glyph_to_msdfgen_shape(const stbtt_fontinfo& font, int glyphIndex)
{
    stbtt_vertex* vertices = nullptr;
    int numVerts = stbtt_GetGlyphShape(&font, glyphIndex, &vertices);

    msdfgen::Shape shape;
    std::optional<msdfgen::Contour> current_contour = std::nullopt;

    for (int i = 0; i < numVerts; ++i)
    {
        stbtt_vertex& v = vertices[i];

        switch (v.type)
        {
            case STBTT_vmove:
                if (current_contour.has_value() && !current_contour->edges.empty())
                {
                    shape.contours.push_back(std::move(current_contour.value()));
                }
                current_contour = msdfgen::Contour();
                break;

            case STBTT_vline:
                if (current_contour.has_value())
                {
                    current_contour->addEdge(msdfgen::EdgeHolder(
                        msdfgen::Point2(vertices[i - 1].x, vertices[i - 1].y),
                        msdfgen::Point2(v.x, v.y)));
                }
                break;

            case STBTT_vcurve:
                if (current_contour.has_value())
                {
                    current_contour->addEdge(msdfgen::EdgeHolder(
                        msdfgen::Point2(vertices[i - 1].x, vertices[i - 1].y),
                        msdfgen::Point2(v.cx, v.cy), msdfgen::Point2(v.x, v.y)));
                }
                break;

            case STBTT_vcubic:
                if (current_contour.has_value())
                {
                    current_contour->addEdge(msdfgen::EdgeHolder(
                        msdfgen::Point2(vertices[i - 1].x, vertices[i - 1].y),
                        msdfgen::Point2(v.cx, v.cy), msdfgen::Point2(v.cx1, v.cy1),
                        msdfgen::Point2(v.x, v.y)));
                }
                break;
        }
    }

    if (current_contour.has_value() && !current_contour->edges.empty())
    {
        shape.contours.push_back(std::move(current_contour.value()));
        current_contour = std::nullopt;
    }

    stbtt_FreeShape(&font, vertices);
    shape.normalize(); // Ensure winding and orientation are correct

    return shape;
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

    msdfgen::Shape shape = stbtt_glyph_to_msdfgen_shape(font.stbtt_font, in_font_index);

    std::array<lm::vec3, RasterizedGlyph::RASTER_SIZE> msdf;

    bool msdf_is_empty = true;
    double internal_units_to_sdf_pixels = 1.0;

    if (!shape.contours.empty())
    {
        msdfgen::edgeColoringByDistance(shape, 3.0);

        msdfgen::Shape::Bounds bounds = shape.getBounds();

        // The scale of the glyph (`internal_units_to_sdf_pixels`) is calculated
        // so that it uses the most space in the texture it can, while leaving
        // enough space around the glyph to fit the padding.

        double padding_in_font_units = SDF_PADDING / (GLYPH_SIZE * font.info.internal_units_to_em);

        double width = bounds.r - bounds.l + padding_in_font_units * 2.0;
        double height = bounds.t - bounds.b + padding_in_font_units * 2.0;

        double scale_x = SDF_SIZE / width;
        double scale_y = SDF_SIZE / height;
        internal_units_to_sdf_pixels = std::min(scale_x, scale_y);

        double center_x = (GLYPH_SLOT_SIZE * 0.5) / internal_units_to_sdf_pixels;
        double center_y = (GLYPH_SLOT_SIZE * 0.5) / internal_units_to_sdf_pixels;

        double shape_center_x = (bounds.l + bounds.r) * 0.5;
        double shape_center_y = (bounds.b + bounds.t) * 0.5;

        msdfgen::Vector2 translate{center_x - shape_center_x, center_y - shape_center_y};

        msdfgen::BitmapRef<float, 3> msdf_ref(
            (float*)msdf.data(), GLYPH_SLOT_SIZE, GLYPH_SLOT_SIZE);

        // Generate MSDF using twice the padding in font units as the range.
        // The range extends from -range/2 to +range/2, so we need 2x padding to get padding
        // distance in each direction
        msdfgen::generateMSDF(
            msdf_ref, shape, padding_in_font_units * 2.0, internal_units_to_sdf_pixels, translate);

        msdf_is_empty = false;
    }

    int left, bottom, right, top;
    stbtt_GetGlyphBox(&font.stbtt_font, in_font_index, &left, &bottom, &right, &top);

    double left_bearing = 0.0;
    {
        int advance_width, left_bearing_int;
        stbtt_GetGlyphHMetrics(&font.stbtt_font, in_font_index, &advance_width, &left_bearing_int);
        left_bearing = left_bearing_int;
    }
    left_bearing *= internal_units_to_sdf_pixels;

    glyph.info.sdf_pixels_to_em = font.info.internal_units_to_em / internal_units_to_sdf_pixels;

    //                   ┌────────────────────────────────────────────────────────┐
    //                   │             :    :                  :                  │
    //        ascent ····│························································│
    //                   │             :    :                  :                  │
    //                   │             :    :                  :                  │
    //           top ····│························xxxxxxxxxxx·····················│
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
    //        bottom ····│·························xxxxxxx.....:..................│
    //                   │             :    :                  :                  │
    //       descent ····│························································│
    //                   │             :    :                  :                  │
    //                   │             :    :                  :                  │
    //                   └────────────────────────────────────────────────────────┘
    //                                 :    :                  :
    //                                 :    :                  :
    //                                 :    :                  :
    //                       left_bearing  left                right
    //
    // `left`, `right`, `bottom`, and `top` are in the font's internal units.
    // `left_bearing` is in SDF pixels, as it is multiplied by
    // `internal_units_to_sdf_pixels`.
    // The advance from the font metrics is not used, as Harfbuzz provides the
    // placements when shaping.
    // `ascent` and `descent` are in em. They are the highest and lowest glyphs
    // can go in the font.
    // `descent` is negative for glyphs that reach below the baseline.
    // `left_bearing` can be negative, in which case it is to the right of
    // `left`.
    //
    // The origin of the glyph, or its pen position, is on the baseline and to
    // the left of the glyph (i.e. at coordinates (`left` - `left_bearing`, 0)
    // in internal font units). It is marked @ in the diagram. All shaping posi-
    // tions (what comes out of Harfbuzz) is relative to this position.
    //
    // The glyph is centered in its SDF, but we want to provide a way to place
    // the glyph according to its origin. The offset allows translating from
    // the top-left corner of the SDF to the origin.
    float offset_x_px =
        (SDF_SIZE * 0.5f - (right - left) * internal_units_to_sdf_pixels * 0.5f - left_bearing);
    float offset_y_px =
        ((SDF_SIZE * 0.5f) + (top - bottom) * internal_units_to_sdf_pixels * 0.5f
         + bottom * internal_units_to_sdf_pixels);

    // Convert the offset to em and negate it, so that it can be simply added
    // to glyph positions when compositing text.
    glyph.info.offset = -lm::vec2(offset_x_px, offset_y_px) * glyph.info.sdf_pixels_to_em;

    if (msdf_is_empty)
    {
        // The glyph has no graphical representation, i.e. it's blank.
        return glyph;
    }

    // Not all fonts have the same winding order for their glyph geometries.
    // This means the SDF can sometimes be inverted.
    // The first pixel is always outside the glyph, so by checking its value
    // we can determine whether the SDF is inverted or not.
    // See https://github.com/Chlumsky/msdfgen/issues/15#issuecomment-278786745
    // The msdfgen executable uses `SimpleTrueShapeDistanceFinder::oneShotDistance`
    // but it's simpler and much likely faster to just check the SDF.
    auto median = [](const lm::vec3& c)
    { return std::max(std::min(c.r, c.g), std::min(std::max(c.r, c.g), c.b)); };

    float first_value = median(msdf[0]);
    bool sdf_is_inverted = first_value > 0.5f;

    for (unsigned int y = 0; y < GLYPH_SLOT_SIZE; ++y)
    {
        for (unsigned int x = 0; x < GLYPH_SLOT_SIZE; ++x)
        {
            auto to_byte = [&](float f)
            {
                f = sdf_is_inverted ? 1.0f - f : f;
                return (uint8_t)std::round(hrz::clamp(f * 255.0f, 0.0f, 255.0f));
            };

            lm::vec3 f = msdf[(GLYPH_SLOT_SIZE - 1 - y) * GLYPH_SLOT_SIZE + x]; // Flip Y axis
            glyph.raster[y * GLYPH_SLOT_SIZE + x] = {to_byte(f.x), to_byte(f.y), to_byte(f.z)};
        }
    }

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
    empty_glyph.sdf_pixels_to_em = 0;
    empty_glyph.offset = lm::vec2(0, 0);

    auto font = rasterizer->fonts.get_object(parsed_font.font_handle);

    if (font == nullptr)
    {
        HRZ_LOG_ERROR("Unknown font handle: {}", parsed_font.font_handle);
        return empty_glyph;
    }

    {
        HRZ_SCOPED_SHARED_LOCK(font->mutex);

        auto it = font->glyphs.find(in_font_index);
        if (it != font->glyphs.end())
        {
            return it->second;
        }
    }

    auto new_glyph = rasterize_glyph(parsed_font, in_font_index);

    HRZ_SCOPED_EXCLUSIVE_LOCK(font->mutex);

    {
        // Another thread may have rasterised the glyph and inserted
        // it in the map since it was checked above, so check again.
        auto it = font->glyphs.find(in_font_index);
        if (it != font->glyphs.end())
        {
            return it->second;
        }
    }

    if (font->glyphs.size() >= MAX_GLYPHS_PER_FONT)
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

    font->glyphs.insert({in_font_index, new_glyph.info});

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

    HRZ_SCOPED_EXCLUSIVE_LOCK(font->mutex);

    assert(!font->new_glyphs.empty());

    std::vector<RasterizedGlyph> new_glyphs{std::move(font->new_glyphs)};
    font->new_glyphs = {};
    font->has_new_glyphs.store(false, std::memory_order_release);

    return {std::move(new_glyphs)};
}
} // namespace font_rasterizer
} // namespace hrz
