#include "hrz/core/vtex/indirection_table_clipmap.h"

#include "hrz/common/profiling.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/resource_context.h"

namespace hrz::vtex
{

// Indirection encoding: (RGBA8UI)
//      R: Page address X
//      G: Page address Y
//      B: LOD (0 = lowest level, 255 = no page cached)
//      A: Unused

IndirectionClipmap::IndirectionClipmap(
    Render* render,
    const ClipmapParams& clipmap_params,
    const monitoring::ResourceOwner& resource_owner,
    MetadataString metadata_key,
    MetadataString metadata_value) :
    _clipmap_params(clipmap_params), _owner(resource_owner)
{
    const uint32_t clip_size = _clipmap_params.get_clip_size();
    const uint32_t lod_count = _clipmap_params.get_lod_count();

    const auto clipmap_offsets = _clipmap_params.get_offsets();
    _offsets.resize(lod_count);
    for (unsigned int lod = 0; lod < lod_count; lod++)
    {
        _offsets[lod] = clipmap_offsets[lod];
    }

    _data = Buffer<IndirectionSlot>(clip_size, clip_size, lod_count);

    {
        my::TextureResource res;
        res.layout.type = my::TextureLayout::Array;
        res.layout.format = my::TextureFormat::RGBA8UI;
        res.layout.width = clip_size;
        res.layout.height = clip_size;
        res.layout.depth = lod_count;
        res.layout.levels = 1;
        res.generate_mipmaps = false;
        res.data = {};

        _texture = render->rc->alloc(
            &res, _owner,
            {{std::move(metadata_key), std::move(metadata_value)},
             {"contents"_ss, "indirection clipmap texture"_ss}});
    }

    {
        my::SamplerResource res;
        res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
        res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
        res.sampler.wrap_x = my::SamplerParams::Wrap::Repeat;
        res.sampler.wrap_y = my::SamplerParams::Wrap::Repeat;
        res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
        res.sampler.is_shadow = false;
        res.use_mipmaps = false;

        _sampler = render->rc->alloc(&res, _owner, {{metadata_key, metadata_value}});
    }

    _slots.reserve(lod_count);

    for (unsigned int lod = 0; lod < lod_count; ++lod)
    {
        uint32_t level_size = _clipmap_params.compute_level_size(lod);

        std::vector<TileSlot> level_slots;
        level_slots.resize(level_size * level_size, TileSlot{{}, ClipmapParams::NoLod});

        _slots.push_back(std::move(level_slots));
    }

    _updated_lod_min = 0;
    _updated_lod_max = lod_count - 1;
    _to_upload_lod_min = 0;
    _to_upload_lod_max = lod_count - 1;
    bake();

    // @Todo Setup a subload callback
}

void IndirectionClipmap::destroy(Render* render)
{
    render->rc->dealloc(_texture);
    render->rc->dealloc(_sampler);
}

bool IndirectionClipmap::is_in_clipmap(TileCoords tile) const
{
    return _clipmap_params.is_in_clipmap(tile);
}

const IndirectionClipmap::TileSlot* IndirectionClipmap::get_tile_slot(TileCoords tile) const
{
    if (!_clipmap_params.is_in_clipmap(tile)) return nullptr;

    TileCoords clipmap_tile = _clipmap_params.source_tile_to_clipmap(tile);
    uint32_t level_size = _clipmap_params.compute_level_size(tile.lod);

    return &_slots[tile.lod][clipmap_tile.x + clipmap_tile.y * level_size];
}

bool IndirectionClipmap::has_tile(TileCoords tile) const
{
    const TileSlot* slot = get_tile_slot(tile);
    if (!slot)
    {
        return false;
    }
    else
    {
        return slot->lod == tile.lod;
    }
}

PageTable::Address IndirectionClipmap::get_page_table_address(TileCoords tile) const
{
    const TileSlot* slot = get_tile_slot(tile);
    if (!slot)
    {
        return PageTable::NoAddress;
    }
    else
    {
        return slot->address;
    }
}

void IndirectionClipmap::evict(TileCoords tile)
{
    if (!is_in_clipmap(tile)) return;

    TileCoords clipmap_tile = _clipmap_params.source_tile_to_clipmap(tile);
    uint32_t level_size = _clipmap_params.compute_level_size(tile.lod);
    TileSlot& slot_in_place = _slots[tile.lod][clipmap_tile.x + clipmap_tile.y * level_size];

    if (slot_in_place.lod == tile.lod)
    {
        // Remove tile from the hierarchy
        propagate_address_down(tile, tile.lod, TileSlot{{}, ClipmapParams::NoLod});

        // Propagate parent tile down to the freed slots
        if (tile.lod > 0)
        {
            TileCoords parent_tile(tile.x / 2, tile.y / 2, tile.lod - 1);

            TileCoords clipmap_parent_tile = _clipmap_params.source_tile_to_clipmap(parent_tile);
            uint32_t parent_level_size = _clipmap_params.compute_level_size(parent_tile.lod);

            TileSlot parent_slot =
                _slots[parent_tile.lod]
                      [clipmap_parent_tile.x + clipmap_parent_tile.y * parent_level_size];
            propagate_address_down(tile, parent_tile.lod, parent_slot);
        }
    }
}

void IndirectionClipmap::assign_tile_address(TileCoords tile, PageTable::Address address)
{
    propagate_address_down(tile, tile.lod, TileSlot{address, (uint8_t)tile.lod});
}

void IndirectionClipmap::propagate_address_down(
    TileCoords tile,
    uint32_t lod_threshold,
    TileSlot slot)
{
    if (!is_in_clipmap(tile)) return;

    TileCoords clipmap_tile = _clipmap_params.source_tile_to_clipmap(tile);
    uint32_t level_size = _clipmap_params.compute_level_size(tile.lod);

    assert(clipmap_tile.x < level_size && clipmap_tile.y < level_size);

    TileSlot& slot_in_place = _slots[tile.lod][clipmap_tile.x + clipmap_tile.y * level_size];
    if (slot_in_place.lod == ClipmapParams::NoLod || slot_in_place.lod <= lod_threshold)
    {
        slot_in_place = slot;

        _updated_lod_min = std::min(_updated_lod_min, (uint8_t)tile.lod);
        _updated_lod_max = std::max(_updated_lod_max, (uint8_t)tile.lod);

        TileCoords child_tile_0{tile.x * 2 + 0, tile.y * 2 + 0, (uint8_t)(tile.lod + 1)};
        TileCoords child_tile_1{tile.x * 2 + 1, tile.y * 2 + 0, (uint8_t)(tile.lod + 1)};
        TileCoords child_tile_2{tile.x * 2 + 0, tile.y * 2 + 1, (uint8_t)(tile.lod + 1)};
        TileCoords child_tile_3{tile.x * 2 + 1, tile.y * 2 + 1, (uint8_t)(tile.lod + 1)};

        propagate_address_down(child_tile_0, lod_threshold, slot);
        propagate_address_down(child_tile_1, lod_threshold, slot);
        propagate_address_down(child_tile_2, lod_threshold, slot);
        propagate_address_down(child_tile_3, lod_threshold, slot);
    }
}

void IndirectionClipmap::bake()
{
    HRZ_SCOPED_SAMPLE("indirection clipmap bake");

    if (_updated_lod_min > _updated_lod_max) return;

    const uint32_t lod_count = _clipmap_params.get_lod_count();

    for (uint8_t lod = _updated_lod_min; lod <= _updated_lod_max; ++lod)
    {
        const uint32_t level_size = _clipmap_params.compute_level_size(lod);
        std::span<const TileSlot> slots = _slots[lod];

        assert(slots.size() == level_size * level_size);

        for (uint32_t y = 0; y < level_size; ++y)
        {
            for (uint32_t x = 0; x < level_size; ++x)
            {
                uint32_t index = x + y * level_size;
                IndirectionSlot& out = _data(x, y, lod);

                out.page_x = slots[index].address.tx;
                out.page_y = slots[index].address.ty;
                out.lod = slots[index].lod;
            }
        }
    }

    if (_updated_lod_min <= _updated_lod_max)
    {
        _need_to_upload = true;
        _to_upload_lod_min = std::min(_to_upload_lod_min, _updated_lod_min);
        _to_upload_lod_max = std::max(_to_upload_lod_max, _updated_lod_max);
    }

    _updated_lod_min = lod_count - 1;
    _updated_lod_max = 0;
}

void IndirectionClipmap::work_gpu(Render* render)
{
    if (!_need_to_upload) return;

    const uint32_t clip_size = _clipmap_params.get_clip_size();
    const uint32_t lod_count = _clipmap_params.get_lod_count();
    const uint32_t to_upload_depth = _to_upload_lod_max - _to_upload_lod_min + 1;

    assert(to_upload_depth > 0);

    {
        HRZ_SCOPED_SAMPLE("indirection clipmap texture upload");
        render->my->update_texture(
            _texture, my::TextureFormat::RGBA8UI, 0, 0, 0, _to_upload_lod_min, clip_size, clip_size,
            to_upload_depth, std::as_bytes(_data.as_span(0, 0, _to_upload_lod_min)));
    }

    _need_to_upload = false;
    _to_upload_lod_min = lod_count - 1;
    _to_upload_lod_max = 0;
}

void IndirectionClipmap::recenter(const ClipmapParams& clipmap_params)
{
    HRZ_SCOPED_SAMPLE("indirection clipmap set center");

    _clipmap_params = clipmap_params;

    const uint32_t lod_count = _clipmap_params.get_lod_count();
    const uint32_t pyramid_count = _clipmap_params.get_pyramid_count();
    const uint32_t clip_size = _clipmap_params.get_clip_size();
    const auto clipmap_offsets = _clipmap_params.get_offsets();

    for (uint32_t lod = pyramid_count; lod < lod_count; ++lod)
    {
        const lm::uvec2 old_offset = _offsets[lod];
        const lm::uvec2 new_offset = clipmap_offsets[lod];

        if (new_offset == old_offset) continue;

        _offsets[lod] = new_offset;

        // If this level moved, update slots by offsetting them.

        int32_t dx = (int32_t)new_offset.x - (int32_t)old_offset.x;
        int32_t dy = (int32_t)new_offset.y - (int32_t)old_offset.y;

        std::vector<TileSlot> new_slots(clip_size * clip_size);
        std::span<TileSlot> slots = _slots[lod];

        // Then we can move the pyramid

        const uint32_t full_level_size = 1 << lod;

        for (int32_t new_y = 0; new_y < (int32_t)clip_size; ++new_y)
        {
            for (int32_t new_x = 0; new_x < (int32_t)clip_size; ++new_x)
            {
                int32_t old_x = new_x + dx;
                int32_t old_y = new_y + dy;

                if (old_x >= 0 && old_x < (int32_t)clip_size && old_y >= 0
                    && old_y < (int32_t)clip_size)
                {
                    const auto& slot = slots[old_x + old_y * clip_size];
                    new_slots[new_x + new_y * clip_size] = slot;
                }
                else
                {
                    // New tile is out of bounds of previous clipmap,
                    // we need to fetch the parent tile, which has been updated
                    // by now because we move the pyramid from top to bottom.

                    uint32_t tile_x = new_x + _offsets[lod].x;
                    uint32_t tile_y = new_y + _offsets[lod].y;

                    uint32_t parent_x = (tile_x / 2 + full_level_size / 2 - _offsets[lod - 1].x)
                        % (full_level_size / 2);
                    uint32_t parent_y = tile_y / 2 - _offsets[lod - 1].y;

                    assert(
                        parent_x >= 0 && parent_x < clip_size && parent_y >= 0
                        && parent_y < clip_size);

                    const auto& slot = _slots[lod - 1][parent_x + parent_y * clip_size];
                    new_slots[new_x + new_y * clip_size] = slot;
                }
            }
        }

        for (unsigned int i = 0; i < slots.size(); ++i)
        {
            slots[i] = new_slots[i];
        }

        _updated_lod_min = std::min(_updated_lod_min, (uint8_t)lod);
        _updated_lod_max = std::max(_updated_lod_max, (uint8_t)lod);
    }

    bake();
}

} // namespace hrz::vtex
