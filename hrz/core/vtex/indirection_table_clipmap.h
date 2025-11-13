#pragma once

#include "hrz/common/metadata.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/tile_coords.h"
#include "hrz/core/buffer.h"
#include "hrz/core/render.h"
#include "hrz/core/vtex/clipmap_params.h"
#include "hrz/core/vtex/page_table.h"

#include <lin_maths.h>
#include <mycelium/backend.h>

#include <span>
#include <string_view>

namespace hrz::vtex
{
class IndirectionClipmap
{
    struct TileSlot
    {
        PageTable::Address address;
        uint8_t lod;
    };

    struct IndirectionSlot
    {
        uint8_t page_x;
        uint8_t page_y;
        uint8_t lod;
        uint8_t _pad;
    };

    static_assert(sizeof(IndirectionSlot) == 4, "IndirectionSlot size");

public:
    IndirectionClipmap(
        Render*,
        const ClipmapParams& clipmap_params,
        const monitoring::ResourceOwner& resource_owner,
        MetadataString metadata_key,
        MetadataString metadata_value);

    void destroy(Render*);

    my::ResourceHandle get_texture() const { return _texture; }

    my::ResourceHandle get_sampler() const { return _sampler; }

    void assign_tile_address(TileCoords tile, PageTable::Address address);
    void evict(TileCoords tile);
    bool is_in_clipmap(TileCoords tile) const;
    bool has_tile(TileCoords tile) const;
    PageTable::Address get_page_table_address(TileCoords tile) const;
    void bake();
    void work_gpu(Render* render);

    void recenter(const ClipmapParams& clipmap_params);

private:
    const TileSlot* get_tile_slot(TileCoords tile) const;

    // Propagate 'slot' down the hierarchy starting from 'tile' when
    // the current lod in slot in NoLod or <= 'lod_threshold'.
    void propagate_address_down(TileCoords tile, uint32_t lod_threshold, TileSlot slot);

    ClipmapParams _clipmap_params;
    monitoring::ResourceOwner _owner;

    uint8_t _updated_lod_min;
    uint8_t _updated_lod_max;

    bool _need_to_upload = false;
    uint8_t _to_upload_lod_min;
    uint8_t _to_upload_lod_max;

    std::vector<lm::uvec2> _offsets;
    std::vector<std::vector<TileSlot>> _slots;
    my::ResourceHandle _texture;
    my::ResourceHandle _sampler;
    Buffer<IndirectionSlot> _data;
};

} // namespace hrz::vtex
