// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/color.h"

#include "hrz/fnd/char_utils.h"

namespace hrz
{
namespace color
{
namespace detail
{

alignas(hrz::L1CacheLineSize) float srgb_to_linear_lut[256];
alignas(hrz::L1CacheLineSize) uint8_t linear_to_srgb_lut[4096];

} // namespace detail

// When `std::pow` is `constexpr`, (C++26) this function can be
// replaced with `consteval` functions to initialise the LUTs.
// This would avoid having to call it at runtime. However computing
// the values is fast, and not embedding the LUTs in the binary
// saves space.
void initialize_srgb_luts()
{
    for (size_t i = 0; i < 256; ++i)
    {
        float srgb_val = i / 255.0F;
        detail::srgb_to_linear_lut[i] = detail::srgb_to_linear(srgb_val);
    }

    for (size_t i = 0; i < 4096; ++i)
    {
        float linear_val = i / 4095.0F;
        detail::linear_to_srgb_lut[i] = static_cast<uint8_t>(
            hrz::clamp(detail::linear_to_srgb(linear_val), 0.0F, 1.0F) * 255.0F + 0.5F);
    }
}

} // namespace color

namespace
{

static constexpr size_t kMaxHtmlColorLength = 20;

std::optional<uint32_t> html_color_lookup_aqua(std::string_view str)
{
    if (str.empty())
    {
        return 0xffffff00U;
    }
    switch (str[0])
    {
        case 'm':
            if (str == "marine")
            {
                return 0xffd4ff7fU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_a(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'l':
            if (str == "liceblue")
            {
                return 0xfffff8f0U;
            }
            break;
        case 'n':
            if (str == "ntiquewhite")
            {
                return 0xffd7ebfaU;
            }
            break;
        case 'q':
            if (str.starts_with("qua"))
            {
                return html_color_lookup_aqua(str.substr(3));
            }
            break;
        case 'z':
            if (str == "zure")
            {
                return 0xfffffff0U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_bla(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'c':
            if (str == "ck")
            {
                return 0xff000000U;
            }
            break;
        case 'n':
            if (str == "nchedalmond")
            {
                return 0xffcdebffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_blue(std::string_view str)
{
    if (str.empty())
    {
        return 0xffff0000U;
    }
    switch (str[0])
    {
        case 'v':
            if (str == "violet")
            {
                return 0xffe22b8aU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_bl(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str.starts_with("a"))
            {
                return html_color_lookup_bla(str.substr(1));
            }
            break;
        case 'u':
            if (str.starts_with("ue"))
            {
                return html_color_lookup_blue(str.substr(2));
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_b(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'l':
            if (str.starts_with("l"))
            {
                return html_color_lookup_bl(str.substr(1));
            }
            break;
        case 'e':
            if (str == "eige")
            {
                return 0xffdcf5f5U;
            }
            break;
        case 'i':
            if (str == "isque")
            {
                return 0xffc4e4ffU;
            }
            break;
        case 'r':
            if (str == "rown")
            {
                return 0xff2a2aa5U;
            }
            break;
        case 'u':
            if (str == "urlywood")
            {
                return 0xff87b8deU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_ch(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "artreuse")
            {
                return 0xff00ff7fU;
            }
            break;
        case 'o':
            if (str == "ocolate")
            {
                return 0xff1e69d2U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_corn(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'f':
            if (str == "flowerblue")
            {
                return 0xffed9564U;
            }
            break;
        case 's':
            if (str == "silk")
            {
                return 0xffdcf8ffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_cor(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'n':
            if (str.starts_with("n"))
            {
                return html_color_lookup_corn(str.substr(1));
            }
            break;
        case 'a':
            if (str == "al")
            {
                return 0xff507fffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_c(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'h':
            if (str.starts_with("h"))
            {
                return html_color_lookup_ch(str.substr(1));
            }
            break;
        case 'a':
            if (str == "adetblue")
            {
                return 0xffa09e5fU;
            }
            break;
        case 'o':
            if (str.starts_with("or"))
            {
                return html_color_lookup_cor(str.substr(2));
            }
            break;
        case 'r':
            if (str == "rimson")
            {
                return 0xff3c14dcU;
            }
            break;
        case 'y':
            if (str == "yan")
            {
                return 0xffffff00U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_darkgre(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'e':
            if (str == "en")
            {
                return 0xff006400U;
            }
            break;
        case 'y':
            if (str == "y")
            {
                return 0xffa9a9a9U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_darkgr(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'e':
            if (str.starts_with("e"))
            {
                return html_color_lookup_darkgre(str.substr(1));
            }
            break;
        case 'a':
            if (str == "ay")
            {
                return 0xffa9a9a9U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_darkg(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'r':
            if (str.starts_with("r"))
            {
                return html_color_lookup_darkgr(str.substr(1));
            }
            break;
        case 'o':
            if (str == "oldenrod")
            {
                return 0xff0b86b8U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_darkor(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "ange")
            {
                return 0xff008cffU;
            }
            break;
        case 'c':
            if (str == "chid")
            {
                return 0xffcc3299U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_darko(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'r':
            if (str.starts_with("r"))
            {
                return html_color_lookup_darkor(str.substr(1));
            }
            break;
        case 'l':
            if (str == "livegreen")
            {
                return 0xff2f6b55U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_darkslategr(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "ay")
            {
                return 0xff4f4f2fU;
            }
            break;
        case 'e':
            if (str == "ey")
            {
                return 0xff4f4f2fU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_darkslate(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'b':
            if (str == "blue")
            {
                return 0xff8b3d48U;
            }
            break;
        case 'g':
            if (str.starts_with("gr"))
            {
                return html_color_lookup_darkslategr(str.substr(2));
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_darks(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "almon")
            {
                return 0xff7a96e9U;
            }
            break;
        case 'e':
            if (str == "eagreen")
            {
                return 0xff8fbc8fU;
            }
            break;
        case 'l':
            if (str.starts_with("late"))
            {
                return html_color_lookup_darkslate(str.substr(4));
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_dark(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'g':
            if (str.starts_with("g"))
            {
                return html_color_lookup_darkg(str.substr(1));
            }
            break;
        case 'o':
            if (str.starts_with("o"))
            {
                return html_color_lookup_darko(str.substr(1));
            }
            break;
        case 's':
            if (str.starts_with("s"))
            {
                return html_color_lookup_darks(str.substr(1));
            }
            break;
        case 'b':
            if (str == "blue")
            {
                return 0xff8b0000U;
            }
            break;
        case 'c':
            if (str == "cyan")
            {
                return 0xff8b8b00U;
            }
            break;
        case 'k':
            if (str == "khaki")
            {
                return 0xff6bb7bdU;
            }
            break;
        case 'm':
            if (str == "magenta")
            {
                return 0xff8b008bU;
            }
            break;
        case 'r':
            if (str == "red")
            {
                return 0xff00008bU;
            }
            break;
        case 't':
            if (str == "turquoise")
            {
                return 0xffd1ce00U;
            }
            break;
        case 'v':
            if (str == "violet")
            {
                return 0xffd30094U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_deep(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'p':
            if (str == "pink")
            {
                return 0xff9314ffU;
            }
            break;
        case 's':
            if (str == "skyblue")
            {
                return 0xffffbf00U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_dimgr(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "ay")
            {
                return 0xff696969U;
            }
            break;
        case 'e':
            if (str == "ey")
            {
                return 0xff696969U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_d(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str.starts_with("ark"))
            {
                return html_color_lookup_dark(str.substr(3));
            }
            break;
        case 'e':
            if (str.starts_with("eep"))
            {
                return html_color_lookup_deep(str.substr(3));
            }
            break;
        case 'i':
            if (str.starts_with("imgr"))
            {
                return html_color_lookup_dimgr(str.substr(4));
            }
            break;
        case 'o':
            if (str == "odgerblue")
            {
                return 0xffff901eU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_f(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'i':
            if (str == "irebrick")
            {
                return 0xff2222b2U;
            }
            break;
        case 'l':
            if (str == "loralwhite")
            {
                return 0xfff0faffU;
            }
            break;
        case 'o':
            if (str == "orestgreen")
            {
                return 0xff228b22U;
            }
            break;
        case 'u':
            if (str == "uchsia")
            {
                return 0xffff00ffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_green(std::string_view str)
{
    if (str.empty())
    {
        return 0xff008000U;
    }
    switch (str[0])
    {
        case 'y':
            if (str == "yellow")
            {
                return 0xff2fffadU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_gre(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'e':
            if (str.starts_with("en"))
            {
                return html_color_lookup_green(str.substr(2));
            }
            break;
        case 'y':
            if (str == "y")
            {
                return 0xff808080U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_gr(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'e':
            if (str.starts_with("e"))
            {
                return html_color_lookup_gre(str.substr(1));
            }
            break;
        case 'a':
            if (str == "ay")
            {
                return 0xff808080U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_gold(std::string_view str)
{
    if (str.empty())
    {
        return 0xff00d7ffU;
    }
    switch (str[0])
    {
        case 'e':
            if (str == "enrod")
            {
                return 0xff20a5daU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_g(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'r':
            if (str.starts_with("r"))
            {
                return html_color_lookup_gr(str.substr(1));
            }
            break;
        case 'a':
            if (str == "ainsboro")
            {
                return 0xffdcdcdcU;
            }
            break;
        case 'h':
            if (str == "hostwhite")
            {
                return 0xfffff8f8U;
            }
            break;
        case 'o':
            if (str.starts_with("old"))
            {
                return html_color_lookup_gold(str.substr(3));
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_indi(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "anred")
            {
                return 0xff5c5ccdU;
            }
            break;
        case 'g':
            if (str == "go")
            {
                return 0xff82004bU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_i(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'n':
            if (str.starts_with("ndi"))
            {
                return html_color_lookup_indi(str.substr(3));
            }
            break;
        case 'v':
            if (str == "vory")
            {
                return 0xfff0ffffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_lavender(std::string_view str)
{
    if (str.empty())
    {
        return 0xfffae6e6U;
    }
    switch (str[0])
    {
        case 'b':
            if (str == "blush")
            {
                return 0xfff5f0ffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_la(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'v':
            if (str.starts_with("vender"))
            {
                return html_color_lookup_lavender(str.substr(6));
            }
            break;
        case 'w':
            if (str == "wngreen")
            {
                return 0xff00fc7cU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_lightc(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'o':
            if (str == "oral")
            {
                return 0xff8080f0U;
            }
            break;
        case 'y':
            if (str == "yan")
            {
                return 0xffffffe0U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_lightgre(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'e':
            if (str == "en")
            {
                return 0xff90ee90U;
            }
            break;
        case 'y':
            if (str == "y")
            {
                return 0xffd3d3d3U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_lightgr(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'e':
            if (str.starts_with("e"))
            {
                return html_color_lookup_lightgre(str.substr(1));
            }
            break;
        case 'a':
            if (str == "ay")
            {
                return 0xffd3d3d3U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_lightg(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'r':
            if (str.starts_with("r"))
            {
                return html_color_lookup_lightgr(str.substr(1));
            }
            break;
        case 'o':
            if (str == "oldenrodyellow")
            {
                return 0xffd2fafaU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_lightslategr(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "ay")
            {
                return 0xff998877U;
            }
            break;
        case 'e':
            if (str == "ey")
            {
                return 0xff998877U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_lights(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "almon")
            {
                return 0xff7aa0ffU;
            }
            break;
        case 'e':
            if (str == "eagreen")
            {
                return 0xffaab220U;
            }
            break;
        case 'k':
            if (str == "kyblue")
            {
                return 0xffface87U;
            }
            break;
        case 'l':
            if (str.starts_with("lategr"))
            {
                return html_color_lookup_lightslategr(str.substr(6));
            }
            break;
        case 't':
            if (str == "teelblue")
            {
                return 0xffdec4b0U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_light(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'c':
            if (str.starts_with("c"))
            {
                return html_color_lookup_lightc(str.substr(1));
            }
            break;
        case 'g':
            if (str.starts_with("g"))
            {
                return html_color_lookup_lightg(str.substr(1));
            }
            break;
        case 's':
            if (str.starts_with("s"))
            {
                return html_color_lookup_lights(str.substr(1));
            }
            break;
        case 'b':
            if (str == "blue")
            {
                return 0xffe6d8adU;
            }
            break;
        case 'p':
            if (str == "pink")
            {
                return 0xffc1b6ffU;
            }
            break;
        case 'y':
            if (str == "yellow")
            {
                return 0xffe0ffffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_lime(std::string_view str)
{
    if (str.empty())
    {
        return 0xff00ff00U;
    }
    switch (str[0])
    {
        case 'g':
            if (str == "green")
            {
                return 0xff32cd32U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_li(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'g':
            if (str.starts_with("ght"))
            {
                return html_color_lookup_light(str.substr(3));
            }
            break;
        case 'm':
            if (str.starts_with("me"))
            {
                return html_color_lookup_lime(str.substr(2));
            }
            break;
        case 'n':
            if (str == "nen")
            {
                return 0xffe6f0faU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_l(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str.starts_with("a"))
            {
                return html_color_lookup_la(str.substr(1));
            }
            break;
        case 'i':
            if (str.starts_with("i"))
            {
                return html_color_lookup_li(str.substr(1));
            }
            break;
        case 'e':
            if (str == "emonchiffon")
            {
                return 0xffcdfaffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_ma(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'g':
            if (str == "genta")
            {
                return 0xffff00ffU;
            }
            break;
        case 'r':
            if (str == "roon")
            {
                return 0xff000080U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_mi(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'd':
            if (str == "dnightblue")
            {
                return 0xff701919U;
            }
            break;
        case 'n':
            if (str == "ntcream")
            {
                return 0xfffafff5U;
            }
            break;
        case 's':
            if (str == "styrose")
            {
                return 0xffe1e4ffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_mediums(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'e':
            if (str == "eagreen")
            {
                return 0xff71b33cU;
            }
            break;
        case 'l':
            if (str == "lateblue")
            {
                return 0xffee687bU;
            }
            break;
        case 'p':
            if (str == "pringgreen")
            {
                return 0xff9afa00U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_medium(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 's':
            if (str.starts_with("s"))
            {
                return html_color_lookup_mediums(str.substr(1));
            }
            break;
        case 'a':
            if (str == "aquamarine")
            {
                return 0xffaacd66U;
            }
            break;
        case 'b':
            if (str == "blue")
            {
                return 0xffcd0000U;
            }
            break;
        case 'o':
            if (str == "orchid")
            {
                return 0xffd355baU;
            }
            break;
        case 'p':
            if (str == "purple")
            {
                return 0xffdb7093U;
            }
            break;
        case 't':
            if (str == "turquoise")
            {
                return 0xffccd148U;
            }
            break;
        case 'v':
            if (str == "violetred")
            {
                return 0xff8515c7U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_m(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str.starts_with("a"))
            {
                return html_color_lookup_ma(str.substr(1));
            }
            break;
        case 'i':
            if (str.starts_with("i"))
            {
                return html_color_lookup_mi(str.substr(1));
            }
            break;
        case 'e':
            if (str.starts_with("edium"))
            {
                return html_color_lookup_medium(str.substr(5));
            }
            break;
        case 'o':
            if (str == "occasin")
            {
                return 0xffb5e4ffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_olive(std::string_view str)
{
    if (str.empty())
    {
        return 0xff008080U;
    }
    switch (str[0])
    {
        case 'd':
            if (str == "drab")
            {
                return 0xff238e6bU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_ol(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'd':
            if (str == "dlace")
            {
                return 0xffe6f5fdU;
            }
            break;
        case 'i':
            if (str.starts_with("ive"))
            {
                return html_color_lookup_olive(str.substr(3));
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_orange(std::string_view str)
{
    if (str.empty())
    {
        return 0xff00a5ffU;
    }
    switch (str[0])
    {
        case 'r':
            if (str == "red")
            {
                return 0xff0045ffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_or(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str.starts_with("ange"))
            {
                return html_color_lookup_orange(str.substr(4));
            }
            break;
        case 'c':
            if (str == "chid")
            {
                return 0xffd670daU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_o(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'l':
            if (str.starts_with("l"))
            {
                return html_color_lookup_ol(str.substr(1));
            }
            break;
        case 'r':
            if (str.starts_with("r"))
            {
                return html_color_lookup_or(str.substr(1));
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_paleg(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'o':
            if (str == "oldenrod")
            {
                return 0xffaae8eeU;
            }
            break;
        case 'r':
            if (str == "reen")
            {
                return 0xff98fb98U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_pale(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'g':
            if (str.starts_with("g"))
            {
                return html_color_lookup_paleg(str.substr(1));
            }
            break;
        case 't':
            if (str == "turquoise")
            {
                return 0xffeeeeafU;
            }
            break;
        case 'v':
            if (str == "violetred")
            {
                return 0xff9370dbU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_pa(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'l':
            if (str.starts_with("le"))
            {
                return html_color_lookup_pale(str.substr(2));
            }
            break;
        case 'p':
            if (str == "payawhip")
            {
                return 0xffd5efffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_pe(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "achpuff")
            {
                return 0xffb9daffU;
            }
            break;
        case 'r':
            if (str == "ru")
            {
                return 0xff3f85cdU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_p(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str.starts_with("a"))
            {
                return html_color_lookup_pa(str.substr(1));
            }
            break;
        case 'e':
            if (str.starts_with("e"))
            {
                return html_color_lookup_pe(str.substr(1));
            }
            break;
        case 'i':
            if (str == "ink")
            {
                return 0xffcbc0ffU;
            }
            break;
        case 'l':
            if (str == "lum")
            {
                return 0xffdda0ddU;
            }
            break;
        case 'o':
            if (str == "owderblue")
            {
                return 0xffe6e0b0U;
            }
            break;
        case 'u':
            if (str == "urple")
            {
                return 0xff800080U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_ro(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 's':
            if (str == "sybrown")
            {
                return 0xff8f8fbcU;
            }
            break;
        case 'y':
            if (str == "yalblue")
            {
                return 0xffe16941U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_r(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'o':
            if (str.starts_with("o"))
            {
                return html_color_lookup_ro(str.substr(1));
            }
            break;
        case 'e':
            if (str == "ed")
            {
                return 0xff0000ffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_sa(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'd':
            if (str == "ddlebrown")
            {
                return 0xff13458bU;
            }
            break;
        case 'l':
            if (str == "lmon")
            {
                return 0xff7280faU;
            }
            break;
        case 'n':
            if (str == "ndybrown")
            {
                return 0xff60a4f4U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_si(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'e':
            if (str == "enna")
            {
                return 0xff2d52a0U;
            }
            break;
        case 'l':
            if (str == "lver")
            {
                return 0xffc0c0c0U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_sea(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'g':
            if (str == "green")
            {
                return 0xff578b2eU;
            }
            break;
        case 's':
            if (str == "shell")
            {
                return 0xffeef5ffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_slategr(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "ay")
            {
                return 0xff908070U;
            }
            break;
        case 'e':
            if (str == "ey")
            {
                return 0xff908070U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_slate(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'b':
            if (str == "blue")
            {
                return 0xffcd5a6aU;
            }
            break;
        case 'g':
            if (str.starts_with("gr"))
            {
                return html_color_lookup_slategr(str.substr(2));
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_s(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str.starts_with("a"))
            {
                return html_color_lookup_sa(str.substr(1));
            }
            break;
        case 'i':
            if (str.starts_with("i"))
            {
                return html_color_lookup_si(str.substr(1));
            }
            break;
        case 'e':
            if (str.starts_with("ea"))
            {
                return html_color_lookup_sea(str.substr(2));
            }
            break;
        case 'k':
            if (str == "kyblue")
            {
                return 0xffebce87U;
            }
            break;
        case 'l':
            if (str.starts_with("late"))
            {
                return html_color_lookup_slate(str.substr(4));
            }
            break;
        case 'n':
            if (str == "now")
            {
                return 0xfffafaffU;
            }
            break;
        case 'p':
            if (str == "pringgreen")
            {
                return 0xff7fff00U;
            }
            break;
        case 't':
            if (str == "teelblue")
            {
                return 0xffb48246U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_t(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "an")
            {
                return 0xff8cb4d2U;
            }
            break;
        case 'e':
            if (str == "eal")
            {
                return 0xff808000U;
            }
            break;
        case 'h':
            if (str == "histle")
            {
                return 0xffd8bfd8U;
            }
            break;
        case 'o':
            if (str == "omato")
            {
                return 0xff4763ffU;
            }
            break;
        case 'u':
            if (str == "urquoise")
            {
                return 0xffd0e040U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_ho(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'n':
            if (str == "neydew")
            {
                return 0xfff0fff0U;
            }
            break;
        case 't':
            if (str == "tpink")
            {
                return 0xffb469ffU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_nav(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str == "ajowhite")
            {
                return 0xffaddeffU;
            }
            break;
        case 'y':
            if (str == "y")
            {
                return 0xff800000U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_white(std::string_view str)
{
    if (str.empty())
    {
        return 0xffffffffU;
    }
    switch (str[0])
    {
        case 's':
            if (str == "smoke")
            {
                return 0xfff5f5f5U;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_wh(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'e':
            if (str == "eat")
            {
                return 0xffb3def5U;
            }
            break;
        case 'i':
            if (str.starts_with("ite"))
            {
                return html_color_lookup_white(str.substr(3));
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup_yellow(std::string_view str)
{
    if (str.empty())
    {
        return 0xff00ffffU;
    }
    switch (str[0])
    {
        case 'g':
            if (str == "green")
            {
                return 0xff32cd9aU;
            }
            break;
    }
    return std::nullopt;
}

std::optional<uint32_t> html_color_lookup(std::string_view str)
{
    if (str.empty()) return std::nullopt;
    switch (str[0])
    {
        case 'a':
            if (str.starts_with("a"))
            {
                return html_color_lookup_a(str.substr(1));
            }
            break;
        case 'b':
            if (str.starts_with("b"))
            {
                return html_color_lookup_b(str.substr(1));
            }
            break;
        case 'c':
            if (str.starts_with("c"))
            {
                return html_color_lookup_c(str.substr(1));
            }
            break;
        case 'd':
            if (str.starts_with("d"))
            {
                return html_color_lookup_d(str.substr(1));
            }
            break;
        case 'f':
            if (str.starts_with("f"))
            {
                return html_color_lookup_f(str.substr(1));
            }
            break;
        case 'g':
            if (str.starts_with("g"))
            {
                return html_color_lookup_g(str.substr(1));
            }
            break;
        case 'i':
            if (str.starts_with("i"))
            {
                return html_color_lookup_i(str.substr(1));
            }
            break;
        case 'l':
            if (str.starts_with("l"))
            {
                return html_color_lookup_l(str.substr(1));
            }
            break;
        case 'm':
            if (str.starts_with("m"))
            {
                return html_color_lookup_m(str.substr(1));
            }
            break;
        case 'o':
            if (str.starts_with("o"))
            {
                return html_color_lookup_o(str.substr(1));
            }
            break;
        case 'p':
            if (str.starts_with("p"))
            {
                return html_color_lookup_p(str.substr(1));
            }
            break;
        case 'r':
            if (str.starts_with("r"))
            {
                return html_color_lookup_r(str.substr(1));
            }
            break;
        case 's':
            if (str.starts_with("s"))
            {
                return html_color_lookup_s(str.substr(1));
            }
            break;
        case 't':
            if (str.starts_with("t"))
            {
                return html_color_lookup_t(str.substr(1));
            }
            break;
        case 'h':
            if (str.starts_with("ho"))
            {
                return html_color_lookup_ho(str.substr(2));
            }
            break;
        case 'k':
            if (str == "khaki")
            {
                return 0xff8ce6f0U;
            }
            break;
        case 'n':
            if (str.starts_with("nav"))
            {
                return html_color_lookup_nav(str.substr(3));
            }
            break;
        case 'v':
            if (str == "violet")
            {
                return 0xffee82eeU;
            }
            break;
        case 'w':
            if (str.starts_with("wh"))
            {
                return html_color_lookup_wh(str.substr(2));
            }
            break;
        case 'y':
            if (str.starts_with("yellow"))
            {
                return html_color_lookup_yellow(str.substr(6));
            }
            break;
    }
    return std::nullopt;
}

} // anonymous namespace

inline static lm::vec3 glsl_mod(const lm::vec3& x, float y)
{
    return x - lm::floor(x / y) * y;
}

// From https://www.shadertoy.com/view/lsS3Wc
lm::vec3 hsl_to_rgb(const lm::vec3& hsl)
{
    lm::vec3 rgb = lm::clamp(
        lm::abs(glsl_mod(lm::vec3(hsl.x * 6) + lm::vec3(0, 4, 2), 6) - lm::vec3(3)) - lm::vec3(1.5),
        lm::vec3(-0.5), lm::vec3(0.5));

    return lm::vec3(hsl.z) + hsl.y * rgb * (1.0F - std::abs(2.0F * hsl.z - 1.0F));
}

// From https://www.shadertoy.com/view/lsS3Wc
lm::vec3 hsv_to_rgb(const lm::vec3& c)
{
    lm::vec3 rgb = lm::clamp(
        lm::abs(glsl_mod(lm::vec3(c.x * 6) + lm::vec3(0, 4, 2), 6) - lm::vec3(3)) - lm::vec3(1),
        lm::vec3(0), lm::vec3(1));
    return c.z * lm::mix(lm::vec3(1), rgb, c.y);
}

// From https://www.shadertoy.com/view/lsS3Wc
lm::vec3 rgb_to_hsv(const lm::vec3& c)
{
    static constexpr float eps = 0.0000001;
    static constexpr lm::vec4 k(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);

    lm::vec4 p = (c.z < c.y) ? lm::vec4(c.y, c.z, k.x, k.y) : lm::vec4(c.z, c.y, k.w, k.z);
    lm::vec4 q = (p.x < c.x) ? lm::vec4(c.x, p.y, p.z, p.x) : lm::vec4(p.x, p.y, p.w, c.x);
    float d = q.x - std::min(q.w, q.y);
    return lm::vec3(abs(q.z + (q.w - q.y) / (6.0 * d + eps)), d / (q.x + eps), q.x);
}

// See https://bottosson.github.io/posts/oklab/
lm::vec4 linear_to_oklab(const lm::vec4& rgb_lin)
{
    static const lm::mat3 lin_rgb_to_lin_lms{
        {0.4122214708F, 0.2119034982F, 0.0883024619F},
        {0.5363325363F, 0.6806995451F, 0.2817188376F},
        {0.0514459929F, 0.1073969566F, 0.6299787005F}
    };

    lm::vec3 lms_lin = lin_rgb_to_lin_lms * rgb_lin.rgb;
    lm::vec3 lms = {std::cbrtf(lms_lin.x), std::cbrtf(lms_lin.y), std::cbrtf(lms_lin.z)};

    // We skip the LMS -> OkLab transform because LMS is already in the linear space we'll
    // interpolate the colors in.
    return lm::vec4(lms, rgb_lin.a);
}

// See https://bottosson.github.io/posts/oklab/
lm::vec4 oklab_to_linear(const lm::vec4& lms)
{
    // We skip the OkLab -> LMS transform because see above.

    lm::vec3 lms_lin = lms.xyz * lms.xyz * lms.xyz;

    static const lm::mat3 lin_lms_to_lin_rgb{
        {+4.0767416621F, -1.2684380046F, -0.0041960863F},
        {-3.3077115913F, +2.6097574011F, -0.7034186147F},
        {+0.2309699292F, -0.3413193965F, +1.7076147010F}
    };

    lm::vec3 rgb_lin = lin_lms_to_lin_rgb * lms_lin;
    rgb_lin = lm::clamp(rgb_lin, lm::vec3(0.0F), lm::vec3(1.0F));

    return lm::vec4(rgb_lin, lms.a);
}

lm::vec4 srgb_to_oklab(const lm::vec4& srgb)
{
    return hrz::linear_to_oklab(hrz::srgb_to_linear(srgb));
}

lm::vec4 oklab_to_srgb(const lm::vec4& lms)
{
    return hrz::linear_to_srgb(hrz::oklab_to_linear(lms));
}

lm::ubvec4 mix_srgb_colors_in_linear(const lm::ubvec4& x, const lm::ubvec4& y, float t)
{
    const lm::vec4 x_lin = hrz::srgb_to_linear_lut(x);
    const lm::vec4 y_lin = hrz::srgb_to_linear_lut(y);
    const lm::vec4 mix_lin = lm::mix(x_lin, y_lin, t);
    return hrz::linear_to_srgb_lut(mix_lin);
}

lm::ubvec4 mix_srgb_colors_in_oklab(const lm::ubvec4& x, const lm::ubvec4& y, float t)
{
    const lm::vec4 x_oklab = srgb_to_oklab(convert_byte_color_to_rgba(x));
    const lm::vec4 y_oklab = srgb_to_oklab(convert_byte_color_to_rgba(y));
    const lm::vec4 mix_oklab = lm::mix(x_oklab, y_oklab, t);
    return convert_rgba_color_to_bytes(oklab_to_srgb(mix_oklab));
}

lm::ubvec4 premultiply_alpha(const lm::ubvec4& rgba)
{
    float alpha = (float)rgba.a / 255.0F;

    auto premultiply_channel = [&](uint8_t c) { return (uint8_t)std::round((float)c * alpha); };

    return {
        premultiply_channel(rgba.r), premultiply_channel(rgba.g), premultiply_channel(rgba.b),
        rgba.a
    };
}

std::optional<uint32_t> parse_html_color_string(std::string_view str)
{
    if (str.size() > kMaxHtmlColorLength) return std::nullopt;

    char color_lower[kMaxHtmlColorLength];
    for (size_t i = 0; i < str.size(); ++i)
    {
        color_lower[i] = hrz::ascii_to_lower(str[i]);
    }

    return html_color_lookup({color_lower, str.size()});
}

std::optional<uint32_t> parse_color_string(std::string_view str)
{
    if (str.empty()) return std::nullopt;

    if (str[0] != '#') return parse_html_color_string(str);

    if (str.size() < 4 || str.size() > 9) return {};

    for (uint8_t i = 1; i < str.size(); ++i)
    {
        if (!is_ascii_hexdigit(str[i])) return {};
    }

    char buffer[9];
    if (str.size() == 9)
    {
        memcpy(buffer, str.data() + 1, 8);
    }
    else if (str.size() == 7)
    {
        memcpy(buffer, str.data() + 1, 6);
        buffer[6] = buffer[7] = 'f';
    }
    else if (str.size() == 5)
    {
        buffer[0] = buffer[1] = str[1];
        buffer[2] = buffer[3] = str[2];
        buffer[4] = buffer[5] = str[3];
        buffer[6] = buffer[7] = str[4];
    }
    else if (str.size() == 4)
    {
        buffer[0] = buffer[1] = str[1];
        buffer[2] = buffer[3] = str[2];
        buffer[4] = buffer[5] = str[3];
        buffer[6] = buffer[7] = 'f';
    }
    else
    {
        return {};
    }

    buffer[8] = 0;
    const uint32_t color = (uint32_t)strtoull(buffer, nullptr, 16);
    // Store colors in little-endian.
    return ((color & 0x000000ff) << 24) | ((color & 0xff000000) >> 24) | ((color & 0x00ff0000) >> 8)
        | ((color & 0x0000ff00) << 8);
}

std::string make_color_string(
    uint32_t c,
    std::string_view prefix,
    bool include_alpha,
    bool uppercase)
{
    std::string s;
    s.reserve(prefix.size() + (include_alpha ? 8 : 6));

    for (char c : prefix)
    {
        s += c;
    }

    uint8_t r = (uint8_t)(c & 0xff);
    uint8_t g = (uint8_t)((c & 0xff00) >> 8);
    uint8_t b = (uint8_t)((c & 0xff0000) >> 16);

    auto to_hex_digit = [&](uint8_t d) -> char
    {
        if (d >= 0 && d <= 9) return '0' + d;
        if (d >= 0xa && d <= 0xf) return (uppercase ? 'A' : 'a') + (d - 0xa);
        assert(false);
        return '?';
    };

    auto append_component = [&](uint8_t comp)
    {
        s += to_hex_digit((comp & 0xf0) >> 4);
        s += to_hex_digit(comp & 0xf);
    };

    append_component(r);
    append_component(g);
    append_component(b);

    if (include_alpha)
    {
        uint8_t a = (uint8_t)((c & 0xff000000) >> 24);
        append_component(a);
    }

    return s;
}

} // namespace hrz
