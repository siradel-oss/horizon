// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_image.h"
#include "hrz/common/metadata.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/tile_coords.h"
#include "hrz/fnd/flat_hash_map.h"

#include <mycelium/backend.h>

namespace hrz
{

struct BlobAllocator;
struct Render;

} // namespace hrz

namespace hrz::vtex
{

class PageTable
{
    struct ImageToUpload
    {
        BlobImage img;
        int x;
        int y;
    };

public:
    struct Address
    {
        uint16_t tx;
        uint16_t ty;

        constexpr bool operator ==(const Address& a) const = default;

        template<typename H>
        friend H AbslHashValue(H h, const Address& a)
        {
            return H::combine(std::move(h), a.tx, a.ty);
        }
    };

    static constexpr Address NoAddress = PageTable::Address{0xffff, 0xffff};

    struct UploadResult
    {
        Address new_address;
        bool evicted;
        TileCoords evicted_tile;
    };

    PageTable(
        Render* render,
        uint32_t page_size,
        uint32_t page_count_x,
        uint32_t page_count_y,
        my::TextureFormat format,
        const monitoring::ResourceOwner& resource_owner,
        MetadataString metadata_key,
        MetadataString metadata_value);

    void destroy(Render* render);
    void work_gpu(Render* render, BlobAllocator* blob_allocator);

    my::TextureFormat get_format() const { return _format; }

    my::ResourceHandle get_texture() const { return _texture; }

    UploadResult upload_page(BlobImage&& img, TileCoords tile);
    bool evict_page(TileCoords tile);

    Address get_address_for_tile(TileCoords tile) const;

    void touch(Address address);

private:
    struct Page
    {
        double last_touch_time;
        TileCoords tile;
        bool occupied;
    };

    inline uint32_t page_id(uint32_t tx, uint32_t ty) { return tx + ty * _page_count_x; }

    inline uint32_t page_id(const Address& address)
    {
        return page_id((uint32_t)address.tx, (uint32_t)address.ty);
    }

    TileCoords recycle_one_page();
    void sort_pages();

    monitoring::ResourceOwner resource_owner;

    uint32_t _page_size;
    uint32_t _page_count_x;
    uint32_t _page_count_y;
    my::TextureFormat _format;
    my::ResourceHandle _texture;

    // Order _pages by last_touch_time. Only contains occupied pages.
    std::vector<Address> _page_order;

    std::vector<Page> _pages;
    std::vector<Address> _free;

    hrz::flat_hash_map<TileCoords, Address> _tile_addresses;
    std::vector<ImageToUpload> _to_upload;
};

} // namespace hrz::vtex
