// SPDX-FileCopyrightText: Copyright 2018 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/vtex/page_cache_manager.h"

namespace hrz::vtex
{

PageCacheManager::UploadResult PageCacheManager::upload_page(BlobImage&& img, TileCoords tile)
{
    PageTable::UploadResult upload = _page_table->upload_page(std::move(img), tile);

    if (upload.evicted)
    {
        _indirection_clipmap->evict(upload.evicted_tile);
    }

    _indirection_clipmap->assign_tile_address(tile, upload.new_address);

    UploadResult result{upload.evicted, upload.evicted_tile};
    return result;
}

bool PageCacheManager::evict_page(TileCoords tile)
{
    bool was_is_page_table = _page_table->evict_page(tile);
    _indirection_clipmap->evict(tile);

    return was_is_page_table;
}

void PageCacheManager::recenter(const ClipmapParams& clipmap_params)
{
    _indirection_clipmap->recenter(clipmap_params);
}

void PageCacheManager::work_gpu(Render* render, BlobAllocator* blob_allocator)
{
    _indirection_clipmap->work_gpu(render);
    _page_table->work_gpu(render, blob_allocator);
}

void PageCacheManager::destroy(Render* render)
{
    _indirection_clipmap->destroy(render);
    _page_table->destroy(render);
}

} // namespace hrz::vtex
