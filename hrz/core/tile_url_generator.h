// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hrz
{

struct TileUrlGenerator
{
    virtual std::string make_url(uint32_t x, uint32_t y, uint32_t z) = 0;

    virtual ~TileUrlGenerator() = default;
};

std::string tile_coords_to_quadkey(uint32_t x, uint32_t y, uint32_t lod);

// Here {-y} is replaced by {ry}, and the subdomain pattern {a-b},
// is replaced by {sub}, and a and b and placed in subdomain_first
// and subdomain_last.
struct PatternTileUrlGenerator : public TileUrlGenerator
{
    std::string base;
    char subdomain_first = 'a';
    char subdomain_last = 'a';

    // This is an optimization: used to not construct the
    // quadkey if it's not necessary.
    bool has_quadkey = false;

    unsigned int level_zero_tile_count_y = 1;

    PatternTileUrlGenerator() = default;

    // Constructs a TileUrl from a URL pattern.
    // Recognized patterns are:
    // - {x}, {y}, {z}
    // - {-y} (reverse y)
    // - {[a-zA-Z0-9]-[a-zA-Z0-9]} (subdomain range)
    // - {quadkey}
    PatternTileUrlGenerator(std::string_view url, unsigned int tile_count_at_level_0_y);

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        return ((const PatternTileUrlGenerator*)this)->make_url(x, y, z);
    }

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) const;
};

// Cycle through multiple `PatternTileUrlGenerator`s to generate URLs.
struct MultiPatternTileUrlGenerator : public TileUrlGenerator
{
    std::vector<PatternTileUrlGenerator> generators;

    MultiPatternTileUrlGenerator() = default;

    // Constructs a TileUrl from a list of URL patterns.
    // See parse_tile_url() for supported patterns.
    MultiPatternTileUrlGenerator(
        std::span<const std::string> urls,
        unsigned int tile_count_at_level_0_y);

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) override
    {
        return ((const MultiPatternTileUrlGenerator*)this)->make_url(x, y, z);
    }

    std::string make_url(uint32_t x, uint32_t y, uint32_t z) const;
};

} // namespace hrz
