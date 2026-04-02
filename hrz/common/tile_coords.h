#pragma once

#include "hrz/protocol/tile.pb.h"

#include <assert.h>
#include <lin_maths.h>

#include <stdint.h>

namespace hrz
{

struct TileCoords
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint8_t lod = 0;

    constexpr TileCoords() = default;

    constexpr TileCoords(uint32_t x, uint32_t y, uint8_t lod) : x(x), y(y), lod(lod) {}

    friend constexpr bool operator ==(const TileCoords&, const TileCoords&) = default;

    inline TileCoords parent() const
    {
        assert(lod > 0);
        TileCoords t;
        t.x = x / 2;
        t.y = y / 2;
        t.lod = lod - 1;
        return t;
    }

    template<typename H>
    friend H AbslHashValue(H h, const TileCoords& t)
    {
        return H::combine(std::move(h), t.x, t.y, t.lod);
    }
};

// This defines an ordering for TileCoords. It's not defined on the struct itself because
// no true ordering exists and this is just a convention used in some places.
constexpr bool tile_coords_ordering_lod_y_x(const TileCoords& a, const TileCoords& b)
{
    if (a.lod != b.lod) return a.lod < b.lod;
    if (a.y != b.y) return a.y < b.y;
    return a.x < b.x;
}

inline hrz_proto::TileCoords to_proto(const TileCoords& c)
{
    hrz_proto::TileCoords t;
    t.set_x(c.x);
    t.set_y(c.y);
    t.set_lod(c.lod);
    return t;
}

inline TileCoords from_proto(const hrz_proto::TileCoords& c)
{
    TileCoords t;
    t.x = c.x();
    t.y = c.y();
    t.lod = (uint8_t)c.lod();
    return t;
}

template<typename T>
struct TileToTileUvTransform
{
    T scale;
    lm::Vector<T, 2> offset;

    TileToTileUvTransform(TileCoords from, TileCoords to)
    {
        const int lod_diff = to.lod - from.lod;
        scale = std::pow((T)2, (T)lod_diff);
        offset.x = (T)from.x * scale - (T)to.x;
        offset.y = (T)from.y * scale - (T)to.y;
    }

    constexpr lm::Vector<T, 2> operator ()(lm::Vector<T, 2> v) { return v * scale + offset; }
};

} // namespace hrz

namespace hrz
{

inline bool tile_usage_comp(
    const TileCoords& t1,
    int t1_uses,
    int t1_past_uses,
    const TileCoords& t2,
    int t2_uses,
    int t2_past_uses)
{
    if (t1_uses != t2_uses)
    {
        if (t1_uses == 0) return false;
        if (t2_uses == 0) return true;
    }

    if (t1_uses != t2_uses)
    {
        return t1_uses > t2_uses;
    }
    else if (t1_past_uses != t2_past_uses)
    {
        return t1_past_uses > t2_past_uses;
    }
    else if (t1.lod != t2.lod)
    {
        return t1.lod > t2.lod;
    }
    else if (t1.y != t2.y)
    {
        return t1.y < t2.y;
    }
    return t1.x < t2.x;
};

} // namespace hrz
