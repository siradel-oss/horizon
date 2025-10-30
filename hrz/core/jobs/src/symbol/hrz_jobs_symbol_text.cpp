#include "symbol/hrz_jobs_symbol_baker.h"

#include <hrz_common_font_rasterizer.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_unicode.h>

#include <limits>

namespace hrz_jobs::symbol
{
using TextInstances = hrz::vt::BakedSymbols::TextInstances;
using GlyphPositionUv = hrz::vt::BakedSymbols::TextInstances::GlyphPositionUv;

namespace
{
static constexpr size_t InitialTextCapacity = 256;
static constexpr size_t InitialGlyphCapacity = 2048;

// Process overview:
//
// Each font has its own glyph texture, containing multi-channel signed distance
// fields (MSDFs, but usually referred to as SDFs in the code base).
// That texture is an atlas of bitmaps GLYPH_SIZE by GLYPH_SIZE pixels large.
// Each bitmap contains an SDF for one glyph.
//
// When baking a text, the first thing is to get the list of the glyphs that
// will be used, as well as their positions. This is achieved thanks to Harfbuzz
// and is a highly complex operation for some scripts (like South Asian ones).
//
// Then all the glyphs that are not already in the font texture are read from
// the font file with stb_truetype to generate a shape structure for msdfgen,
// then rasterised to SDFs with msdfgen, and finally added to the texture.
//
// Combining the glyph positions and the font atlas, meshes are generated. Each
// glyph is drawn using a quad (actually a pair of triangles). The quads are
// placed in 2D space, as if only one text (i.e a single string) was composed
// and drawn to a flat surface.
//
// Each text is identified by its index within a tile. The vertices have an
// attribute giving the index of the text. Then textures are used to store
// per-text data: in-tile position, scale, and colours.
//
// (In practice, the actual draw call uses instanced rendering, with each glyph
// constituting an instance. This allows not having to use indexed rendering,
// and having to allocate an index buffer for each tile.)
//
// Each font uses an arbitrary scale when defining its glyphs. The same glyph
// can be 200 units high or 3,000. This is internal to the font and irrespective
// to how big the font is supposed to be compared to other fonts. The bounds of
// the msdfgen shape are used to determine how each glyph should be scaled in
// order for its characters to fit in a box of a given size. (Not all glyphs in
// a single font are rasterised to the same scale, so that large glyphs can fit
// into their allocated area, and small glyphs use the most space available to
// improve visual quality.) So the first scaling factor for a glyph maps from
// the font's internal units to the size of the glyph's SDF.
//
// The font size is given in pixels by em. One em is about the height of the
// text. The `stbtt_ScaleForMappingEmToPixels()` is called to know how to scale
// the font's internal units to ems. This is the second scaling factor.
//
// With these values we can make all glyphs fit into bitmaps whose size is pre-
// determined, and scale these bitmaps so that the text size is consistent.
// The scale factors for a glyph are combined in `Glyph::sdf_pixels_to_em`.

struct TextRunBakingResult
{
    unsigned int baked_char_count;
    float text_run_width_em;
    unsigned int baked_lines_count;
};

struct GlyphLineInfo
{
    unsigned int glyphs_to_bake_count = 0;
    float glyphs_to_bake_width_em = 0;
    unsigned int chars_to_bake_count = 0;
};

struct BreakableGlyphInfo
{
    unsigned int glyph_index = 0;
    float glyph_run_width_em = 0;
    unsigned int char_index = 0;
    unsigned int char_byte_size = 0;
};

// This function takes a string without any `\n` character, shapes it, and splits
// it into multiple lines if needed to respect the maximum available height.
// In order to split a baked string into two lines at a given position, two condi-
// tions must be respected:
// * The baked text can be split at the position without breaking any multi-glyph
//   text shaping. This is checked with Harfbuzz's HB_GLYPH_FLAG_UNSAFE_TO_BREAK
//   flag.
// * It must be acceptable with respect to the language's line break rules. This
//   is a much harder problem. This line breaking algorithm is described in Unicode
//   Standard Annex #14. It's very complex, supposes that the language used in the
//   string is known, and for multiple languages requires dictionaries. (In fact,
//   in some cases, semantic analysis is required, and human input is necessary.)
// This function uses a much simpler algorithm, that breaks lines at the last place
// possible, that is the furthest along the string that does not overflow on the
// right, while still respecting some rules.
// Line breaks can occur where there is whitespace (and this whitespace is eaten so
// that it does not appear before nor after the break), after hypens, next to a CJK
// character, or anywhere between two characters if no break opportunity has been
// found elsewhere.
TextRunBakingResult bake_text_run(
    uint16_t text_index,
    std::string_view text,
    hrz::font_rasterizer::Font& font,
    float em_size,
    float baseline_em,
    float new_line_height_em,
    hrz_proto::TextAlignment alignment,
    float available_width_em,
    unsigned int available_lines,
    hrz::BlobVector<GlyphPositionUv>& glyph_positions_uvs,
    hrz::BlobVector<uint16_t>& text_indices,
    hb_buffer_t* hb_buffer,
    hrz::FontRasterizer* font_rasterizer)
{
    HRZ_SCOPED_SAMPLE("bake text run");

    TextRunBakingResult error_result = {(unsigned int)text.size(), 0.0f, 0};

    hb_buffer_clear_contents(hb_buffer);
    hb_buffer_add_utf8(hb_buffer, text.data(), text.size(), 0, text.size());

    hb_buffer_guess_segment_properties(hb_buffer);

    {
        HRZ_SCOPED_SAMPLE("shape text");
        hb_shape(font.hb_font, hb_buffer, nullptr, 0);
    }

    unsigned int glyph_count = 0;
    hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
    hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(hb_buffer, &glyph_count);
    if ((!glyph_info || !glyph_pos) && glyph_count > 0)
    {
        HRZ_LOG_WARNING("No glyph info, skipping text");
        return error_result;
    }

    auto break_line = [&](unsigned int first_glyph,
                          unsigned int first_char) -> std::optional<GlyphLineInfo>
    {
        // Line break at a place where it can be expected, such as whitespace.
        std::optional<BreakableGlyphInfo> nice_breakable_glyph;

        // Line break between two characters where a line break wouldn't be
        // expected, such as in the middle of a word, but shaping is preserved.
        // this is to prevent the line from exceeding its maximum width.
        std::optional<BreakableGlyphInfo> mediocre_breakable_glyph;

        // Line break between two random glyphs. This is the last resort break,
        // it can be bad looking, but otherwise there is no way to bake any
        // glyphs on the line.
        std::optional<BreakableGlyphInfo> ugly_breakable_glyph;

        // Line break emitted when we don't know yet is the break can be made,
        // because it depends on the next glyph.
        std::optional<BreakableGlyphInfo> previous_breakable_glyph;
        bool previous_glyph_was_breakable_after = false;

        float glyph_run_width_em = 0;
        float trailing_whitespace_width_em = 0;

        for (unsigned int i = first_glyph; i < glyph_count; ++i)
        {
            auto info = glyph_info[i];
            auto char_index = info.cluster;

            if (char_index >= text.size())
            {
                HRZ_LOG_ERROR("Invalid text shaping data");
                return std::nullopt;
            }

            auto code_point_opt = hrz::unicode::get_first_code_point(text.substr(char_index));
            if (!code_point_opt.has_value())
            {
                HRZ_LOG_ERROR("Text is not a valid UTF-8 string");
                return std::nullopt;
            }
            auto code_point = code_point_opt.value();

            auto glyph_is_whitespace = hrz::unicode::is_whitespace(code_point);
            auto glyph_is_hyphen = hrz::unicode::is_hyphen(code_point);
            auto glyph_is_cjk_character = hrz::unicode::is_cjk_character(code_point);

            auto glyph_is_breakable_before = glyph_is_cjk_character;
            auto glyph_is_breakable_after = glyph_is_hyphen || glyph_is_cjk_character;

            // If the unsafe to break flag is present, breaking before the glyph
            // would generate improperly shaped text.
            auto glyph_is_safe_to_break_before =
                (hb_glyph_info_get_glyph_flags(&info) & HB_GLYPH_FLAG_UNSAFE_TO_BREAK) == 0;

            previous_glyph_was_breakable_after &= glyph_is_safe_to_break_before;
            glyph_is_breakable_before &= glyph_is_safe_to_break_before;

            if ((previous_glyph_was_breakable_after || glyph_is_breakable_before)
                && previous_breakable_glyph.has_value())
            {
                nice_breakable_glyph = previous_breakable_glyph;
            }

            float glyph_width_em = glyph_pos[i].x_advance * font.info.internal_units_to_em;
            glyph_run_width_em += glyph_width_em;

            if (glyph_is_whitespace)
            {
                trailing_whitespace_width_em += glyph_width_em;
            }
            else
            {
                trailing_whitespace_width_em = 0;
            }

            // Whitespace doesn't count towards glyphs width when trailing
            // the sub-string. This is so that is discarded when it is replaced
            // by a line break.
            auto glyph_run_width_after = glyph_run_width_em - trailing_whitespace_width_em;

            if (glyph_run_width_after >= available_width_em)
            {
                break;
            }

            BreakableGlyphInfo breakable_glyph_info;
            breakable_glyph_info.glyph_index = i;
            breakable_glyph_info.glyph_run_width_em = glyph_run_width_after;
            breakable_glyph_info.char_index = char_index;
            breakable_glyph_info.char_byte_size = code_point.utf8_byte_size;

            if (glyph_is_safe_to_break_before)
            {
                if (glyph_is_whitespace)
                {
                    nice_breakable_glyph = {breakable_glyph_info};
                }

                if (previous_breakable_glyph.has_value())
                {
                    mediocre_breakable_glyph = breakable_glyph_info;
                }
            }

            if (previous_breakable_glyph.has_value())
            {
                ugly_breakable_glyph = breakable_glyph_info;
            }

            previous_breakable_glyph = {breakable_glyph_info};
            previous_glyph_was_breakable_after = glyph_is_breakable_after;
        }

        if (glyph_run_width_em <= available_width_em)
        {
            return {
                {glyph_count - first_glyph, glyph_run_width_em,
                 (unsigned int)text.size() - first_char}};
        }
        else
        {
            std::optional<BreakableGlyphInfo> breakable_glyph = std::nullopt;
            if (nice_breakable_glyph.has_value())
            {
                breakable_glyph = nice_breakable_glyph;
            }
            else if (mediocre_breakable_glyph.has_value())
            {
                breakable_glyph = mediocre_breakable_glyph;
            }
            else if (ugly_breakable_glyph.has_value())
            {
                breakable_glyph = ugly_breakable_glyph;
            }

            if (breakable_glyph.has_value())
            {
                return {
                    {breakable_glyph->glyph_index + 1 - first_glyph,
                     breakable_glyph->glyph_run_width_em,
                     breakable_glyph->char_index + breakable_glyph->char_byte_size - first_char}};
            }
        }

        HRZ_LOG_WARNING("Could not break text run into lines");
        return std::nullopt;
    };

    unsigned int baked_glyphs = 0;
    unsigned int baked_chars = 0;
    float text_run_width_em = 0;
    unsigned int baked_lines = 0;

    do
    {
        auto glyph_line_opt = break_line(baked_glyphs, baked_chars);
        if (!glyph_line_opt.has_value())
        {
            return error_result;
        }
        auto glyph_line = glyph_line_opt.value();

        auto line_width_em = glyph_line.glyphs_to_bake_width_em;
        text_run_width_em = std::max(text_run_width_em, line_width_em);

        // Alignment does not have any visible impact when there is only one line.
        // When there are multiple lines, they have to be properly aligned.
        // This supposes having the width of the longest line, but we compose
        // the text line by line.
        // To align the text nonetheless, the aligned side is always at x = 0.
        // This is compensated later when all lines have been composed.
        float advance_x_em = 0;
        switch (alignment)
        {
            case hrz_proto::TextAlignment::CENTERED: advance_x_em = -line_width_em / 2; break;
            case hrz_proto::TextAlignment::LEFT_ALIGNED: advance_x_em = 0; break;
            case hrz_proto::TextAlignment::RIGHT_ALIGNED: advance_x_em = -line_width_em; break;
            default: assert(false && "Unhandled case");
        }

        float advance_y_em = baseline_em;

        for (unsigned int i = baked_glyphs;
             i < baked_glyphs + glyph_line.glyphs_to_bake_count && i < glyph_count; ++i)
        {
            unsigned int in_font_glyph_index = glyph_info[i].codepoint;
            auto glyph =
                hrz::font_rasterizer::get_glyph_info(font_rasterizer, font, in_font_glyph_index);

            if (!glyph.is_blank)
            {
                float sdf_size_em = glyph.get_sdf_size_in_em();

                float x_offset_em =
                    glyph_pos[i].x_offset * font.info.internal_units_to_em + glyph.offset.x;
                float y_offset_em =
                    glyph_pos[i].y_offset * font.info.internal_units_to_em + glyph.offset.y;

                float x0 = (advance_x_em + x_offset_em) * em_size;
                float x1 = (advance_x_em + x_offset_em + sdf_size_em) * em_size;
                float y0 = (advance_y_em + y_offset_em) * em_size;
                float y1 = (advance_y_em + y_offset_em + sdf_size_em) * em_size;

                lm::bbox2 uv = glyph.get_uv();

                glyph_positions_uvs.push_back({
                    {x0, y0},
                    {uv.min.x, uv.min.y},
                    {x0, y1},
                    {uv.min.x, uv.max.y},
                    {x1, y1},
                    {uv.max.x, uv.max.y},
                    {x1, y0},
                    {uv.max.x, uv.min.y},
                });

                text_indices.push_back(text_index);
            }

            advance_x_em += glyph_pos[i].x_advance * font.info.internal_units_to_em;
        }

        baseline_em += new_line_height_em;

        baked_glyphs += glyph_line.glyphs_to_bake_count;
        baked_chars += glyph_line.chars_to_bake_count;
        baked_lines += 1;
    } while (baked_glyphs < glyph_count && baked_lines < available_lines);

    return {baked_chars, text_run_width_em, baked_lines};
}

// Returns the text's size if the operation has been successful.
std::optional<lm::vec2> bake_text(
    uint16_t text_index,
    std::string_view text,
    hrz::font_rasterizer::Font& font,
    float em_size,
    float outline_em,
    hrz_proto::TextAlignment alignment,
    float line_spacing,
    float minimum_width,
    float available_width,
    float available_height,
    hrz::BlobVector<GlyphPositionUv>& glyph_positions_uvs,
    hrz::BlobVector<uint16_t>& text_indices,
    hb_buffer_t* hb_buffer,
    hrz::FontRasterizer* font_rasterizer)
{
    HRZ_SCOPED_SAMPLE("bake text");

    if (text.empty()) return {{0, 0}};
    if (em_size <= 0.0f) return {{0, 0}};

    outline_em = std::max(outline_em, 0.0f);
    line_spacing = std::max(line_spacing, 0.0f);

    float available_width_em = available_width / em_size - outline_em * 2;
    float available_height_em = available_height / em_size - outline_em * 2;

    float text_width_em = 0;
    float text_height_em = 0;

    float first_line_height_em = font.info.single_line_height();

    if (available_width <= 0) return {{0, available_height}};
    if (first_line_height_em > available_height_em) return {{0, available_height}};

    float new_line_height_em = font.info.new_line_height() * line_spacing;
    uint32_t max_line_count = std::isinf(available_height_em) || new_line_height_em == 0.0f
        ? std::numeric_limits<uint32_t>::max()
        : (uint32_t)(std::floor((available_height_em - first_line_height_em) / new_line_height_em)
                     + 1);

    auto glyph_positions_uvs_size = glyph_positions_uvs.size();
    if (!glyph_positions_uvs_size.has_value()) return std::nullopt;
    size_t glyph_pos_uv_count_before = glyph_positions_uvs.size().value();

    {
        uint32_t available_lines = max_line_count;
        float baseline_em = font.info.ascent;

        uint32_t start_char = 0;
        uint32_t end_char = 0;

        auto bake_run = [&]()
        {
            std::string_view text_run = {text.data() + start_char, end_char - start_char};
            auto baking_res = bake_text_run(
                text_index, text_run, font, em_size, baseline_em, new_line_height_em, alignment,
                available_width_em, available_lines, glyph_positions_uvs, text_indices, hb_buffer,
                font_rasterizer);
            text_width_em = std::max(text_width_em, baking_res.text_run_width_em);

            if (text_height_em == 0 && baking_res.baked_lines_count > 0)
            {
                text_height_em =
                    first_line_height_em + (baking_res.baked_lines_count - 1) * new_line_height_em;
            }
            else
            {
                text_height_em += baking_res.baked_lines_count * new_line_height_em;
            }

            baseline_em += baking_res.baked_lines_count * new_line_height_em;
            available_lines -= baking_res.baked_lines_count;
            return baking_res.baked_char_count;
        };

        auto bake_line = [&]()
        {
            while (start_char < end_char && available_lines > 0)
            {
                start_char += bake_run();
            }
        };

        auto bake_empty_line = [&]()
        {
            if (available_lines > 0)
            {
                if (text_height_em == 0) text_height_em = first_line_height_em;
                text_height_em += new_line_height_em;
                baseline_em += new_line_height_em;
                available_lines -= 1;
            }
        };

        while (end_char < text.size() && available_lines > 0)
        {
            char c = text.data()[end_char];
            if (c == '\n')
            {
                bake_empty_line();
                end_char += 1;
                start_char = end_char;
            }
            else
            {
                end_char += 1;
                c = text.data()[end_char];
                if (c == '\n')
                {
                    bake_line();
                    end_char += 1;
                    start_char = end_char;
                }
            }
        }

        bake_line();

        if (text.data()[text.size() - 1] == '\n')
        {
            bake_empty_line();
        }
    }

    if (!glyph_positions_uvs.is_valid()) return std::nullopt;

    // When the text is too narrow to fill its minimum width constraint, it is
    // shifted according to the alignment.
    // (The potential missing height is always below the text.)
    float missing_width =
        std::max(0.0f, minimum_width - (text_width_em + outline_em * 2) * em_size);

    // Shift the text according to the alignment, so that the left side of the
    // longest line is at x = 0.
    // The text is also shifted by the size of the outline, so that the outline
    // doesn't extend beyond the bounds of the element.
    lm::vec2 shift = lm::vec2{outline_em, outline_em} * em_size;
    switch (alignment)
    {
        case hrz_proto::TextAlignment::CENTERED:
            shift.x += text_width_em * 0.5f * em_size + missing_width * 0.5f;
            break;
        case hrz_proto::TextAlignment::LEFT_ALIGNED: break;
        case hrz_proto::TextAlignment::RIGHT_ALIGNED:
            shift.x += text_width_em * em_size + missing_width;
            break;
        default: assert(false && "Unhandled case");
    }

    lm::bbox2 text_bbox = lm::bbox2::invalid();

    auto glyph_positions_uvs_data_opt = glyph_positions_uvs.data();
    if (!glyph_positions_uvs_data_opt.has_value()) return std::nullopt;
    auto glyph_positions_uvs_data = glyph_positions_uvs_data_opt.value();

    size_t glyph_pos_uv_count_after = glyph_positions_uvs_data.size();
    for (size_t i = glyph_pos_uv_count_before; i < glyph_pos_uv_count_after; ++i)
    {
        auto& pos_uv = glyph_positions_uvs_data[i];

        pos_uv.xy0 += shift;
        pos_uv.xy1 += shift;
        pos_uv.xy2 += shift;
        pos_uv.xy3 += shift;

        text_bbox = lm::expand(text_bbox, pos_uv.xy0);
        text_bbox = lm::expand(text_bbox, pos_uv.xy1);
        text_bbox = lm::expand(text_bbox, pos_uv.xy2);
        text_bbox = lm::expand(text_bbox, pos_uv.xy3);
    }

    return {lm::vec2{text_width_em + outline_em * 2, text_height_em + outline_em * 2} * em_size};
}

// Data textures can require dummy data to be inserted, due to
// their 2D layout.
template<typename T>
void pad_vector_for_data_texture(hrz::BlobVector<T>& vector)
{
    auto size = vector.size().value_or(0);

    if (size == 0) return;

    if (size % hrz::vt::DATA_TEXTURE_SIZE != 0)
    {
        size = size - (size % hrz::vt::DATA_TEXTURE_SIZE) + hrz::vt::DATA_TEXTURE_SIZE;
    }

    vector.resize(size);
}
} // namespace

hrz::JobResult SymbolBaker::TextVisitor::init()
{
    hb_buffer = hb_buffer_create();

    if (hb_buffer == nullptr)
    {
        HRZ_LOG_ERROR("Could not allocate text buffer");
        return hrz::JobResult::FAILURE;
    }

    hb_buffer_set_flags(hb_buffer, (hb_buffer_flags_t)(HB_BUFFER_FLAG_BOT | HB_BUFFER_FLAG_EOT));

    return hrz::JobResult::SUCCESS;
}

void SymbolBaker::TextVisitor::deinit()
{
    hb_buffer_destroy(hb_buffer);
}

hrz::JobResult SymbolBaker::TextVisitor::init_element_instances(
    const hrz::vt::SymbolBakingData::Element& element)
{
    assert(element.z_index.has_value());

    if (element.anchor_index.has_value())
    {
        assert(element.type == hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT);
        const auto& params = element.text();

        if (!fonts.contains(params.font))
        {
            fonts.insert(
                {params.font,
                 hrz::font_rasterizer::get_font(get_context().get_font_rasterizer(), params.font)});
        }

        auto blob_allocator = get_context().get_blob_allocator();

        instances_by_z_index.insert(
            {element.z_index.value(),
             Instances{
                 0,
                 hrz::BlobVector<lm::mat4>{blob_allocator, InitialTextCapacity},
                 hrz::BlobVector<uint32_t>{blob_allocator, InitialTextCapacity},
                 hrz::BlobVector<float>{blob_allocator, InitialTextCapacity},
                 hrz::BlobVector<lm::ubvec4>{blob_allocator, InitialTextCapacity},
                 hrz::BlobVector<lm::ubvec4>{blob_allocator, InitialTextCapacity},
                 hrz::BlobVector<GlyphPositionUv>{blob_allocator, InitialGlyphCapacity},
                 hrz::BlobVector<uint16_t>{blob_allocator, InitialGlyphCapacity},
                 false,
             }});
    }

    return hrz::JobResult::SUCCESS;
}

ElementGeometry SymbolBaker::TextVisitor::visit_element(
    const hrz::vt::SymbolBakingData::Element& element,
    const SizeConstraints& constraints)
{
    assert(element.type == hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT);
    const auto& params = element.text();

    auto text = std::string_view{params.default_text};
    load_string_property(params.text_prp, &text);

    auto font_size = params.default_font_size;
    load_float_property(params.font_size_prp, &font_size);

    auto outline_size = params.default_outline_size;
    load_float_property(params.outline_size_prp, &outline_size);

    auto fill_color_srgb = params.default_fill_color_srgb;
    load_rgba_color_property(params.fill_color_prp, &fill_color_srgb);

    auto outline_color_srgb = params.default_outline_color_srgb;
    load_rgba_color_property(params.outline_color_prp, &outline_color_srgb);

    auto alignment = params.default_alignment;
    load_enum_property<hrz_proto::TextAlignment>(params.alignment_prp, &alignment);

    auto line_spacing = params.default_line_spacing;
    load_float_property(params.line_spacing_prp, &line_spacing);

    auto size = constraints.min;

    if (text.empty()) return ElementGeometry{size, lm::bbox2{{}, size}};

    if (element.anchor_index.has_value())
    {
        auto& instances = instances_by_z_index.at(element.z_index.value());

        uint16_t text_index = instances.text_count;
        instances.text_count += 1;

        auto& font = fonts.at(params.font);

        float outline_em_size = outline_size;
        if (params.outline_size_unit == hrz_proto::TextOutlineWidthUnit::OUTLINE_WIDTH_IN_FONT_UNIT)
        {
            outline_em_size /= font_size;
        }

        // The outline can only extend into the SDF's padding, so its size is clamped
        // to the edge of the padding.
        outline_em_size = std::min(
            outline_em_size,
            (float)hrz::font_rasterizer::SDF_PADDING / hrz::font_rasterizer::GLYPH_SIZE);

        double padding_in_font_units = hrz::font_rasterizer::SDF_PADDING
            / (hrz::font_rasterizer::GLYPH_SIZE * font.info.internal_units_to_em);
        float outline_size =
            (outline_em_size / font.info.internal_units_to_em) / padding_in_font_units;
        outline_size *= 0.5f;

        // Extenting the outline right next to the edge of the SDF makes the edges of
        // the rendered outline jaggy. So we leave a small gap.
        // (The value has been determined empirically, as a balance between edge
        // smoothness and loss of maximum width.)
        outline_size = hrz::clamp(outline_size, 0.0f, 0.5f - (6.0f / 255.0f));

        auto text_size_opt = bake_text(
            text_index, text, font, font_size, outline_em_size, alignment, line_spacing,
            constraints.min.x, constraints.max.x, constraints.max.y, instances.glyph_positions_uvs,
            instances.text_indices, hb_buffer, get_context().get_font_rasterizer());

        if (!text_size_opt.has_value()) return ElementGeometry{size, lm::bbox2{{}, size}};

        size.x = std::max(size.x, text_size_opt->x);
        size.y = std::max(size.y, text_size_opt->y);

        instances.transforms.push_back(lm::mat4::identity());
        instances.anchor_indices.push_back(element.anchor_index.value());
        instances.outline_widths.push_back(outline_size);
        instances.fill_colors.push_back(fill_color_srgb);
        instances.outline_colors.push_back(outline_color_srgb);

        instances.has_non_zero_outline_width |= outline_em_size > 0.0f;

        register_element_instance_index(text_index);
    }

    return ElementGeometry{size, lm::bbox2{{}, size}};
}

void SymbolBaker::TextVisitor::finalize_element_instance(
    uint32_t z_index,
    uint32_t element_instance_index,
    const lm::mat4& global_transform)
{
    auto& instances = instances_by_z_index.at(z_index);

    auto transform_data = instances.transforms.data();
    if (transform_data.has_value())
    {
        transform_data.value()[element_instance_index] = global_transform;
    }

    auto anchor_index_data = instances.anchor_indices.data();
    if (anchor_index_data.has_value())
    {
        auto& anchor_index = anchor_index_data.value()[element_instance_index];
        anchor_index = get_baked_anchor_index(anchor_index);
    }
}

std::optional<std::optional<hrz::vt::BakedSymbols::ElementInstances>> SymbolBaker::TextVisitor::
    get_element_instances_at_z_index(uint32_t z_index)
{
    auto blob_allocator = get_context().get_blob_allocator();
    const auto& resource_owner = get_context().get_resource_owner();

    auto& instances = instances_by_z_index.at(z_index);

    pad_vector_for_data_texture(instances.transforms);
    pad_vector_for_data_texture(instances.anchor_indices);
    pad_vector_for_data_texture(instances.outline_widths);
    pad_vector_for_data_texture(instances.fill_colors);
    pad_vector_for_data_texture(instances.outline_colors);

    auto transforms_array_opt = instances.transforms.to_blob_array();
    auto anchor_indices_array_opt = instances.anchor_indices.to_blob_array();
    auto outline_widths_array_opt = instances.outline_widths.to_blob_array();
    auto fill_colors_array_opt = instances.fill_colors.to_blob_array();
    auto outline_colors_array_opt = instances.outline_colors.to_blob_array();
    auto glyph_positions_uvs_array_opt = instances.glyph_positions_uvs.to_blob_array();
    auto text_indices_array_opt = instances.text_indices.to_blob_array();
    if (!transforms_array_opt.has_value() || !anchor_indices_array_opt.has_value()
        || !outline_widths_array_opt.has_value() || !fill_colors_array_opt.has_value()
        || !outline_colors_array_opt.has_value() || !glyph_positions_uvs_array_opt.has_value()
        || !text_indices_array_opt.has_value())
    {
        return std::nullopt;
    }

    if (transforms_array_opt->empty())
    {
        // No texts have been generated.
        return {std::optional<hrz::vt::BakedSymbols::ElementInstances>{}};
    }

    transforms_array_opt->register_blob_metadata(
        blob_allocator, "contents"_ss, "text transforms"_ss);
    transforms_array_opt->register_blob_owner(blob_allocator, resource_owner);
    anchor_indices_array_opt->register_blob_metadata(
        blob_allocator, "contents"_ss, "text anchor indices"_ss);
    anchor_indices_array_opt->register_blob_owner(blob_allocator, resource_owner);
    outline_widths_array_opt->register_blob_metadata(
        blob_allocator, "contents"_ss, "text outline widths"_ss);
    outline_widths_array_opt->register_blob_owner(blob_allocator, resource_owner);
    fill_colors_array_opt->register_blob_metadata(
        blob_allocator, "contents"_ss, "text fill colors"_ss);
    fill_colors_array_opt->register_blob_owner(blob_allocator, resource_owner);
    outline_colors_array_opt->register_blob_metadata(
        blob_allocator, "contents"_ss, "text outline colors"_ss);
    outline_colors_array_opt->register_blob_owner(blob_allocator, resource_owner);
    glyph_positions_uvs_array_opt->register_blob_metadata(
        blob_allocator, "contents"_ss, "text glyph positions & uv"_ss);
    glyph_positions_uvs_array_opt->register_blob_owner(blob_allocator, resource_owner);
    text_indices_array_opt->register_blob_metadata(
        blob_allocator, "contents"_ss, "text glyph text indices"_ss);
    text_indices_array_opt->register_blob_owner(blob_allocator, resource_owner);

    return {{hrz::vt::BakedSymbols::ElementInstances{
        hrz_proto::SymbolElementType::TEXT_SYMBOL_ELEMENT,
        TextInstances{
            transforms_array_opt.value(), anchor_indices_array_opt.value(),
            outline_widths_array_opt.value(), fill_colors_array_opt.value(),
            outline_colors_array_opt.value(), glyph_positions_uvs_array_opt.value(),
            text_indices_array_opt.value(), instances.has_non_zero_outline_width}}}};
}
} // namespace hrz_jobs::symbol
