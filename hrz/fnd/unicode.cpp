#include "hrz/fnd/unicode.h"

namespace hrz::unicode
{
// See https://en.wikipedia.org/wiki/UTF-8#Encoding
std::optional<CodePoint> get_first_code_point(std::string_view str)
{
    auto byte = [&](size_t index) { return (uint32_t)str[index]; };

    auto is_continuation_byte = [](uint32_t byte) { return (byte & 0xc0) == 0x80; };

    if (str.empty()) return std::nullopt;

    if ((byte(0) & 0x80) == 0) return {{byte(0), 1}};

    if ((byte(0) & 0xe0) == 0xc0)
    {
        if (str.size() < 2) return std::nullopt;
        if (is_continuation_byte(byte(1)))
        {
            return {{((byte(0) & 0x1f) << 6) + (byte(1) & 0x3f), 2}};
        }
        return std::nullopt;
    }

    if ((byte(0) & 0xf0) == 0xe0)
    {
        if (str.size() < 3) return std::nullopt;
        if (is_continuation_byte(byte(1)) && is_continuation_byte(byte(2)))
        {
            return {{((byte(0) & 0xf) << 12) + ((byte(1) & 0x3f) << 6) + (byte(2) & 0x3f), 3}};
        }
        return std::nullopt;
    }

    if ((byte(0) & 0xf8) == 0xf0)
    {
        if (str.size() < 4) return std::nullopt;
        if (is_continuation_byte(byte(1)) && is_continuation_byte(byte(2))
            && is_continuation_byte(byte(3)))
        {
            return {
                {((byte(0) & 0x7) << 18) + ((byte(1) & 0x3f) << 12) + ((byte(2) & 0x3f) << 6)
                     + (byte(3) & 0x3f),
                 4}};
        }
        return std::nullopt;
    }

    return std::nullopt;
}

// See https://www.unicode.org/reports/tr14/#BA
bool is_whitespace(uint32_t code_point)
{
    return code_point == 0x9     // tab
        || code_point == 0x20    // space
        || code_point == 0x1680  // ogham space mark
        || code_point == 0x2002  // en space
        || code_point == 0x2003  // em space
        || code_point == 0x2004  // three-per-em space
        || code_point == 0x2005  // four-per-em space
        || code_point == 0x2006  // six-per-em space
        || code_point == 0x2008  // punctuation space
        || code_point == 0x2009  // thin space
        || code_point == 0x200a  // hair space
        || code_point == 0x200b  // zero-width space
        || code_point == 0x205f  // medium mathematical space
        || code_point == 0x3000; // ideographic space
}

// See https://www.unicode.org/reports/tr14/#BA
bool is_hyphen(uint32_t code_point)
{
    return code_point == 0x2d    // hyphen minus
        || code_point == 0x58a   // armenian hyphen
        || code_point == 0x2010  // hyphen
        || code_point == 0x2012  // figure dash
        || code_point == 0x2013; // en dash
}

// See https://www.unicode.org/Public/UCD/latest/ucd/Blocks.txt
bool is_cjk_character(uint32_t code_point)
{
    // Comprises the following ranges:
    // * 2E80 — 2EFF      CJK Radicals Supplement
    // * 2F00 — 2FDF      Kangxi Radicals
    // * 2FF0 — 2FFF      Ideographic Description Characters
    // * 3000 — 303F      CJK Symbols and Punctuation
    // * 3040 — 309F      Hiragana
    // * 30A0 — 30FF      Katakana
    // * 3100 — 312F      Bopomofo
    // * 3190 — 319F      Kanbun
    // * 31A0 — 31BF      Bopomofo Extended
    // * 31F0 — 31FF      Katakana Phonetic Extensions
    // * 3200 — 32FF      Enclosed CJK Letters and Months
    // * 3300 — 33FF      CJK Compatibility
    // * 3400 — 4DBF      CJK Unified Ideographs Extension A
    // * 4DC0 — 4DFF      Yijing Hexagram Symbols
    // * 4E00 — 9FFF      CJK Unified Ideographs
    // * A000 — A48F      Yi Syllables
    // * A490 — A4CF      Yi Radicals
    // * F900 — FAFF      CJK Compatibility Ideographs
    // * FE30 — FE4F      CJK Compatibility Forms
    // * 20000 — 2A6DF    CJK Unified Ideographs Extension B
    // * 2A700 — 2B73F    CJK Unified Ideographs Extension C
    // * 2B740 — 2B81F    CJK Unified Ideographs Extension D
    // * 2B820 — 2CEAF    CJK Unified Ideographs Extension E
    // * 2CEB0 — 2EBEF    CJK Unified Ideographs Extension F
    // * 2EBF0 — 2EE5F    CJK Unified Ideographs Extension I
    // * 2F800 — 2FA1F    CJK Compatibility Ideographs Supplement
    // * 30000 — 3134F    CJK Unified Ideographs Extension G
    // * 31350 — 323AF    CJK Unified Ideographs Extension H

    return (code_point >= 0x2e80 && code_point <= 0x312f)
        || (code_point >= 0x3190 && code_point <= 0xa4cf)
        || (code_point >= 0xf900 && code_point <= 0xfaff)
        || (code_point >= 0xfe30 && code_point <= 0xfe4f)
        || (code_point >= 0x20000 && code_point <= 0x323af);
}
} // namespace hrz::unicode
