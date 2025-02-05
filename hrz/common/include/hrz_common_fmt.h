#pragma once

#include "hrz_common_tile_coords.h"

#include <fmt/format.h>

// https://fmt.dev/latest/api.html#formatting-user-defined-types

template<>
struct fmt::formatter<hrz::TileCoords>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) { return ctx.begin(); }

    auto format(const hrz::TileCoords& coords, format_context& ctx) const -> decltype(ctx.out())
    {
        return format_to(ctx.out(), "{}-{}-{}", coords.lod, coords.x, coords.y);
    }
};
