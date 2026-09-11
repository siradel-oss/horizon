// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/tile_url_generator.h"

#include "hrz/fnd/char_utils.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/string_utils.h"

#include <fmt/core.h>

#include <string_view>

namespace hrz
{

PatternTileUrlGenerator::PatternTileUrlGenerator(
    std::string_view url,
    unsigned int level_zero_tile_count_y) :
    base(url),
    subdomain_first('a'),
    subdomain_last('b'),
    has_quadkey(false),
    level_zero_tile_count_y(level_zero_tile_count_y)
{
    if (base.find("{quadkey}") != std::string::npos)
    {
        has_quadkey = true;
    }

    // Replace {-y} by {ry}
    size_t it = base.find("{-y}");
    if (it != std::string::npos)
    {
        base[it + 1] = 'r';
    }

    std::string_view search_span = base;
    bool found_subdomain = false;
    it = 0;

    // Look for {char0-char1}
    while (search_span.size() >= 5)
    {
        if (search_span[0] == '{')
        {
            if (search_span[2] == '-' && search_span[4] == '}'
                && hrz::is_ascii_alpha_numeric(search_span[1])
                && hrz::is_ascii_alpha_numeric(search_span[3]))
            {
                char first = search_span[1];
                char last = search_span[3];
                if (last < first)
                {
                    std::swap(first, last);
                }

                subdomain_first = first;
                subdomain_last = last;
                found_subdomain = true;
                break;
            }
            else
            {
                it += 1;
                search_span = search_span.substr(1);
            }
        }
        else
        {
            int offset = str::find(search_span, '{');
            if (offset < 0) break;
            search_span = search_span.substr(offset);
            it += offset;
        }
    }

    if (found_subdomain)
    {
        base[it + 1] = 's';
        base[it + 2] = 'u';
        base[it + 3] = 'b';
    }

    static constexpr std::string_view kValidArgs[] = {"x", "y", "z", "ry", "sub", "quadkey"};
    base = hrz::str::sanitize_named_fmt_arguments(base, kValidArgs);
}

std::string PatternTileUrlGenerator::make_url(uint32_t x, uint32_t y, uint32_t z) const
{
    // The subdomain index is static based on the tile id instead of random
    // so that it doesn't mess with the cache: a tile will always have the
    // same URL, but not all tiles will be fetched on the same subdomain.
    char subdomain =
        subdomain_first + hrz::hash_values(x, y, z) % (subdomain_last - subdomain_first + 1);

    std::string quadkey;
    if (has_quadkey)
    {
        quadkey = tile_coords_to_quadkey(x, y, z);
    }

    const uint32_t tile_count = (1 << z) * level_zero_tile_count_y;
    const uint32_t ry = tile_count - y - 1;

    return fmt::format(
        fmt::runtime(base), fmt::arg("x", x), fmt::arg("y", y), fmt::arg("z", z),
        fmt::arg("ry", ry), fmt::arg("sub", subdomain), fmt::arg("quadkey", quadkey));
}

MultiPatternTileUrlGenerator::MultiPatternTileUrlGenerator(
    std::span<const std::string> urls,
    unsigned int tile_count_at_level_0_y)
{
    for (const auto& url : urls)
    {
        generators.push_back({url, tile_count_at_level_0_y});
    }
}

std::string MultiPatternTileUrlGenerator::make_url(uint32_t x, uint32_t y, uint32_t z) const
{
    // The generator index is static based on the tile id instead of random
    // so that it doesn't mess with the cache: a tile will always be fetched
    // with the same generator (which in turn garantees that each tile is
    // always fetched with the same URL), but not all tiles will be fetched
    // with the same generator.
    char generator_index = hrz::hash_values(x, y, z) % generators.size();

    return generators.at(generator_index).make_url(x, y, z);
}

// From https://docs.microsoft.com/en-us/bingmaps/articles/bing-maps-tile-system
std::string tile_coords_to_quadkey(uint32_t x, uint32_t y, uint32_t lod)
{
    std::string quadkey;
    quadkey.resize(lod);

    for (unsigned int i = lod; i > 0; --i)
    {
        char digit = '0';
        const uint32_t mask = 1 << (i - 1);
        if ((x & mask) != 0)
        {
            ++digit;
        }
        if ((y & mask) != 0)
        {
            ++digit;
            ++digit;
        }
        quadkey[lod - i] = digit;
    }

    return quadkey;
}

} // namespace hrz
