#include "hrz/core/vtex/page_table.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/profiling.h"
#include "hrz/core/clock.h"
#include "hrz/core/render/context.h"

namespace hrz::vtex
{

PageTable::PageTable(
    Render* render,
    uint32_t page_size,
    uint32_t page_count_x,
    uint32_t page_count_y,
    my::TextureFormat format,
    const monitoring::ResourceOwner& resource_owner,
    MetadataString metadata_key,
    MetadataString metadata_value) :
    resource_owner(resource_owner),
    _page_size(page_size),
    _page_count_x(page_count_x),
    _page_count_y(page_count_y),
    _format(format)
{
    my::TextureLayout layout;
    layout.type = my::TextureLayout::Type2D;
    layout.format = format;
    layout.width = _page_count_x * _page_size;
    layout.height = _page_count_y * _page_size;
    layout.depth = 1;
    layout.levels = 1;

    std::unique_ptr<std::byte[]> data = {};
    std::span<const std::byte> data_span = {};

    if (my::is_format_compressed(format))
    {
        // Compressed textures' data must be explicitly initialised.

        size_t data_size = layout.get_level_byte_size(0);
        data = std::make_unique<std::byte[]>(data_size);
        data_span = std::span<const std::byte>{data.get(), data_size};
    }

    my::TextureResource res;
    res.layout = layout;
    res.data = {&data_span, 1};
    res.generate_mipmaps = false;

    _texture = render->rc->alloc(
        &res, resource_owner,
        {{std::move(metadata_key), std::move(metadata_value)},
         {"contents"_ss, "page table texture"_ss}});

    _free.reserve(_page_count_x * _page_count_y);
    _pages.resize(_page_count_x * _page_count_y);
    _page_order.reserve(_page_count_x * _page_count_y);

    for (uint16_t ty = 0; ty < _page_count_y; ++ty)
    {
        for (uint16_t tx = 0; tx < _page_count_x; ++tx)
        {
            Address address{tx, ty};
            uint32_t pid = page_id(address);

            _free.push_back(address);
            _pages[pid] = Page{0, {0, 0, 0}, false};
        }
    }
}

void PageTable::destroy(Render* render)
{
    render->rc->dealloc(_texture);
}

void PageTable::work_gpu(Render* render, BlobAllocator* blob_allocator)
{
    for (const auto& to_upload : _to_upload)
    {
        // We don't care if the caller tries to upload a tile that doesn't
        // have the correct size. We just make sure it doesn't overflow
        // into a neighboring page.
        uint32_t width = std::min(_page_size, to_upload.img.width());
        uint32_t height = std::min(_page_size, to_upload.img.height());

        if (to_upload.img.valid())
        {
            auto image_data = to_upload.img.data();

            HRZ_SCOPED_SAMPLE_A("vtex page table update texture");
            render->my->update_texture(
                _texture, to_upload.img.format(), 0, to_upload.x, to_upload.y, 0, width, height, 1,
                image_data.as_span());
        }
    }

    _to_upload.clear();
}

PageTable::Address PageTable::get_address_for_tile(TileCoords tile) const
{
    auto it = _tile_addresses.find(tile);

    if (it == _tile_addresses.end())
    {
        return NoAddress;
    }

    return it->second;
}

void PageTable::sort_pages()
{
    std::ranges::sort(
        _page_order, [&](const Address& a, const Address& b)
        { return _pages[page_id(a)].last_touch_time > _pages[page_id(b)].last_touch_time; });
}

TileCoords PageTable::recycle_one_page()
{
    assert(!_page_order.empty());

    sort_pages();

    const Address oldest_page_address = _page_order.back();
    _page_order.pop_back();
    _free.push_back(oldest_page_address);

    const uint32_t pid = page_id(oldest_page_address);
    _pages[pid].occupied = false;
    _tile_addresses.erase(_pages[pid].tile);
    return _pages[pid].tile;
}

PageTable::UploadResult PageTable::upload_page(BlobImage&& img, TileCoords tile)
{
    UploadResult result;
    result.evicted = false;

    {
        // In place upload
        auto page_address = get_address_for_tile(tile);
        if (page_address != NoAddress)
        {
            uint32_t pid = page_id(page_address);
            Page& page = _pages[pid];
            page.last_touch_time = hrz::clock::CurrentFrameRealTime.ms;

            _to_upload.push_back(
                ImageToUpload{
                    std::move(img), (int)(page_address.tx * _page_size),
                    (int)(page_address.ty * _page_size)
                });

            result.new_address = page_address;

            return result;
        }
    }

    if (_free.empty())
    {
        result.evicted = true;
        result.evicted_tile = recycle_one_page();
    }

    assert(!_free.empty());

    Address page_address = _free.back();
    _free.pop_back();

    uint32_t pid = page_id(page_address);
    Page& page = _pages[pid];
    page.occupied = true;
    page.tile = tile;
    page.last_touch_time = hrz::clock::CurrentFrameRealTime.ms;

    _page_order.push_back(page_address);

    _to_upload.push_back(
        ImageToUpload{
            std::move(img), (int)(page_address.tx * _page_size), (int)(page_address.ty * _page_size)
        });

    result.new_address = page_address;

    _tile_addresses.insert({tile, page_address});

    return result;
}

bool PageTable::evict_page(TileCoords tile)
{
    auto page_address = get_address_for_tile(tile);
    if (page_address != NoAddress)
    {
        uint32_t pid = page_id(page_address);
        Page& page = _pages[pid];
        page.occupied = false;
        _tile_addresses.erase(page.tile);

        _free.push_back(page_address);

        return true;
    }

    return false;
}

void PageTable::touch(Address address)
{
    uint32_t pid = page_id(address);
    if (pid < _pages.size())
    {
        _pages[pid].last_touch_time = hrz::clock::CurrentFrameRealTime.ms;
    }
}

} // namespace hrz::vtex
