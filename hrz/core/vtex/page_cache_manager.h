#pragma once

#include "hrz/core/render.h"
#include "hrz/core/vtex/indirection_table_clipmap.h"
#include "hrz/core/vtex/page_table.h"

#include <mycelium/backend.h>

#include <memory>

namespace hrz
{
struct BlobAllocator;
class BlobImage;

namespace vtex
{
class PageCacheManager
{
public:
    struct UploadResult
    {
        bool evicted;
        TileCoords evicted_tile;
    };

    PageCacheManager(
        std::unique_ptr<PageTable> table,
        std::unique_ptr<IndirectionClipmap> indirection) :
        _page_table(std::move(table)), _indirection_clipmap(std::move(indirection))
    {
    }

    // @Todo Not sure if this is the right thing to do.
    const PageTable* get_page_table() const { return _page_table.get(); }

    UploadResult upload_page(BlobImage&& img, TileCoords tile);
    bool evict_page(TileCoords tile);

    void recenter(const ClipmapParams& clipmap_params);

    my::ResourceHandle get_clipmap_texture() const { return _indirection_clipmap->get_texture(); }

    my::ResourceHandle get_clipmap_sampler() const { return _indirection_clipmap->get_sampler(); }

    my::ResourceHandle get_table_texture() const { return _page_table->get_texture(); }

    void work_gpu(Render* render, BlobAllocator* blob_allocator);
    void destroy(Render* render);

    inline void bake_clipmap() { _indirection_clipmap->bake(); }

    inline bool has_tile(TileCoords tile) const { return _indirection_clipmap->has_tile(tile); }

    // Set last touch time on the page table tile, and put the tile
    // in the indirection clipmap if is isn't but is inside the
    // clipmap bounds.
    inline void touch(TileCoords tile) const
    {
        if (has_tile(tile))
        {
            auto address = _indirection_clipmap->get_page_table_address(tile);
            _page_table->touch(address);
        }
        else if (_indirection_clipmap->is_in_clipmap(tile))
        {
            auto address = _page_table->get_address_for_tile(tile);

            if (address != PageTable::NoAddress)
            {
                _indirection_clipmap->assign_tile_address(tile, address);
                _page_table->touch(address);
            }
        }
    }

private:
    std::unique_ptr<PageTable> _page_table;
    std::unique_ptr<IndirectionClipmap> _indirection_clipmap;
};

} // namespace vtex
} // namespace hrz
