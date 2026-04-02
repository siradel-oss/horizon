#include "hrz/core/vtex/clipmap_params.h"

#include "hrz/common/maths.h"
#include "hrz/common/profiling.h"

#include <assert.h>

namespace hrz::vtex
{

//            Full size = 16
//            Clip size =  4
//
//             |          |
//   4 - - \   +----------+   / - +
//          \  |          |  /    |
//   3 - - - \ +----------+ /     |  LOD count = 5
//            \|          |/      |
//   2 - - - - \----------/ - - - | - - - - - +
//              \        /        |           |
//   1 - - - - - \------/         |           |  Pyramid count = 3
//                \    /          |           |
//   0 - - - - - - \--/ - - - - - + - - - - - +

ClipmapParams::ClipmapParams(uint32_t clip_size, uint32_t tile_size, uint32_t lod_count) :
    _clip_size(clip_size), _tile_size(tile_size), _lod_count(lod_count)
{
    assert(lod_count > 0 && lod_count < 30);

    uint32_t level_size = 1;
    _pyramid_count = 1;
    _offsets.reserve(_lod_count);

    for (unsigned int i = 0; i < _lod_count; ++i)
    {
        uint32_t full_level_size = 1 << i;
        uint32_t corner = full_level_size / 2 - level_size / 2;

        _offsets.push_back(lm::uvec2(corner));

        if (level_size < _clip_size)
        {
            _pyramid_count += 1;
            level_size *= 2;
        }
    }
}

lm::uvec2 ClipmapParams::get_offset(uint32_t lod) const
{
    assert(lod >= 0 && lod < _lod_count);

    return _offsets[lod];
}

bool ClipmapParams::is_in_clipmap(TileCoords tile) const
{
    TileCoords clipmap_tile = source_tile_to_clipmap(tile);

    return clipmap_tile.lod != NoLod;
}

void ClipmapParams::set_center(lm::uvec2 new_center)
{
    HRZ_SCOPED_SAMPLE("clipmap set center");

    for (uint32_t lod = _pyramid_count; lod < _lod_count; ++lod)
    {
        const uint32_t lod_from_pyramid_bottom = _lod_count - lod - 1;
        const uint32_t full_level_size = 1 << lod;

        // Align the center to the tiles grid so we don't get the different
        // level of details misaligned.
        lm::uvec2 new_level_center = new_center;
        new_level_center.x = (new_level_center.x >> (lod_from_pyramid_bottom + 1)) << 1;
        new_level_center.y = (new_level_center.y >> (lod_from_pyramid_bottom + 1)) << 1;

        lm::uvec2 new_offset;

        // We try to avoid negative tile coordinates.
        // Also this canonicalizes the coordinates so the checks below don't fail
        // if we just wrapped around the antimeridian.
        // @Note We are not worried about overflow here because we only have
        // 24 levels of detail.
        new_offset.x = (new_level_center.x + full_level_size - _clip_size / 2) % full_level_size;

        // Clamp the clipmap border at the raster extent.
        new_offset.y =
            std::min(std::max(new_level_center.y, _clip_size / 2), full_level_size - _clip_size / 2)
            - _clip_size / 2;

        _offsets[lod] = new_offset;
    }
}

TileCoords ClipmapParams::clipmap_to_source_tile(TileCoords tile) const
{
    static const TileCoords no_tile{0, 0, NoLod};

    if (tile.lod >= _lod_count) return no_tile;

    lm::uvec2 offset = _offsets[tile.lod];
    tile.x += offset.x;
    tile.y += offset.y;

    uint32_t tile_count_at_lod = 1 << tile.lod;

    // If the clipmap is centered near the antimeridian, on its
    // West, and if the tile is East of the antimeridian,
    // the x coordinate can become one Earth revolution too large
    // after the offset is applied.
    tile.x = tile.x % tile_count_at_lod;

    // Sometimes numerical imprecision brings the y coordinate a
    // bit too far towards the South pole.
    tile.y = std::min(tile.y, tile_count_at_lod - 1);

    if (tile.x >= tile_count_at_lod || tile.y >= tile_count_at_lod)
    {
        return no_tile;
    }
    else
    {
        return tile;
    }
}

TileCoords ClipmapParams::source_tile_to_clipmap(TileCoords tile) const
{
    static const TileCoords no_tile{0, 0, NoLod};

    if (tile.lod >= _lod_count) return no_tile;

    uint32_t tile_count_at_lod = 1 << tile.lod;

    if (tile.x >= tile_count_at_lod || tile.y >= tile_count_at_lod)
    {
        return no_tile;
    }

    lm::uvec2 offset = _offsets[tile.lod];

    // If the clipmap is centered near the antimeridian, on its
    // West, and if the tile is East of the antimeridian,
    // the x coordinate would become negative after the offset is
    // applied, if it was signed. Apply a whole Earth revolution
    // to counter this.
    if (tile.x < offset.x)
    {
        tile.x += tile_count_at_lod;
    }

    tile.x -= offset.x;
    tile.y -= offset.y;

    uint32_t level_size = compute_level_size(tile.lod);

    if (tile.x >= level_size || tile.y >= level_size)
    {
        return no_tile;
    }
    else
    {
        return tile;
    }
}

void ClipmapParams::write_offsets(std::span<lm::vec4> data) const
{
    HRZ_SCOPED_SAMPLE("clipmap write ubo data");
    assert(data.size() == _lod_count);

    for (uint8_t lod = 0; lod < _lod_count; ++lod)
    {
        const uint32_t level_size = compute_level_size(lod);
        const uint32_t lod_from_pyramid_bottom = _lod_count - lod - 1;
        const uint32_t tile_size = _tile_size << lod_from_pyramid_bottom;

        lm::uvec2 center = (_offsets[lod] + lm::uvec2(level_size / 2)) * tile_size;

        lm::dvec2 center_d(center);
        lm::vec2 center_partial, center_base;
        hrz::split_vec2d(center_d, center_base, center_partial);

        data[lod] = lm::vec4(center_base, center_partial);
    }
}

} // namespace hrz::vtex
