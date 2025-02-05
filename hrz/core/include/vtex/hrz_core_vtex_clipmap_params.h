#pragma once

#include <hrz_common_tile_coords.h>

#include <gsl/gsl-lite.hpp>
#include <lin_maths.h>

#include <vector>

namespace hrz::vtex
{
class ClipmapParams
{
public:
    enum
    {
        NoLod = 0xffu,
    };

    ClipmapParams(uint32_t clip_size, uint32_t tile_size, uint32_t lod_count);

    void set_center(lm::uvec2 center);

    inline uint32_t get_clip_size() const { return _clip_size; }

    inline uint32_t get_lod_count() const { return _lod_count; }

    inline uint32_t get_pyramid_count() const { return _pyramid_count; }

    inline gsl::span<const lm::uvec2> get_offsets() const { return _offsets; }

    lm::uvec2 get_offset(uint32_t lod) const;

    bool is_in_clipmap(TileCoords tile) const;

    inline uint32_t compute_level_size(uint32_t lod) const
    {
        if (lod < _pyramid_count)
            return 1 << lod;
        else
            return _clip_size;
    }

    TileCoords clipmap_to_source_tile(TileCoords tile) const;

    TileCoords source_tile_to_clipmap(TileCoords tile) const;

    void write_offsets(gsl::span<lm::vec4> offsets) const;

private:
    uint32_t _clip_size;
    uint32_t _tile_size;
    uint32_t _lod_count;
    uint32_t _pyramid_count;

    std::vector<lm::uvec2> _offsets;
};

} // namespace hrz::vtex
