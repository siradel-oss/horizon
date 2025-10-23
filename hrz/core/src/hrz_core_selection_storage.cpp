#include "hrz_core_selection_storage.h"

#include "hrz_core_render.h"

#include <algorithm>

namespace hrz::selection
{
template<typename Indirection>
SelectionStorageUint32Buffer<Indirection>::SelectionStorageUint32Buffer(
    size_t index_count,
    const monitoring::ResourceOwner& resource_owner,
    std::initializer_list<std::pair<MetadataString, MetadataString>> metadata) :
    _indirection(index_count),
    _buffer(my::ResourceHandle::null()),
    _any_selected(false),
    _need_to_update_buffer(false),
    resource_owner(resource_owner)
{
    const size_t bucket_count = (index_count + 31) / 32;
    _bitmap.resize(bucket_count, 0);

    for (const auto& it : metadata)
    {
        _metadata.emplace_back(it.first, it.second);
    }
}

template<typename Indirection>
void SelectionStorageUint32Buffer<Indirection>::update_selection(
    const hrz::flat_hash_set<uint64_t>& selected_objects)
{
    if (_any_selected || !selected_objects.empty())
    {
        std::ranges::fill(_bitmap, 0);
    }

    const bool had_any_selected = _any_selected;
    _any_selected = false;

    if (!selected_objects.empty())
    {
        for (auto object_id : selected_objects)
        {
            auto mark_index_as_selected = [&](uint32_t index)
            {
                const uint32_t bucket_index = index / 32;
                const uint32_t bit_index = index % 32;

                _bitmap[bucket_index] |= (1 << bit_index);
                _any_selected = true;
            };

            const FeatureIdIndirections indirections = _indirection.get_indirections(object_id);
            if (indirections.has_range())
            {
                for (auto index : indirections.range())
                {
                    mark_index_as_selected(index);
                }
            }
            else
            {
                mark_index_as_selected(indirections.single_index());
            }
        }
    }

    if (had_any_selected || _any_selected)
    {
        _need_to_update_buffer = true;
    }
}

template<typename Indirection>
void SelectionStorageUint32Buffer<Indirection>::work_gpu(Render* render)
{
    if (!_buffer)
    {
        my::BufferResource res(my::BufferResource::BufferType::Vertex);
        res.size = _bitmap.size() * sizeof(uint32_t);
        res.usage = my::UsageHint::Updatable;
        res.data = nullptr;

        _buffer = render->rc->alloc(&res, resource_owner, _metadata);
        render->rc->monitoring->register_gpu_resource_metadata(
            _buffer, "selection storage"_ss, ""_ss);
    }

    if (_need_to_update_buffer)
    {
        render->my->update_buffer(_buffer, 0, _bitmap.size() * sizeof(uint32_t), _bitmap.data());
        _need_to_update_buffer = false;
    }
}

template<typename Indirection>
SelectionStorageUint32Texture<Indirection>::SelectionStorageUint32Texture(
    size_t index_count,
    const monitoring::ResourceOwner& resource_owner,
    std::initializer_list<std::pair<MetadataString, MetadataString>> metadata) :
    _indirection(index_count),
    _texture(my::ResourceHandle::null()),
    _need_to_update_texture(false),
    _any_selected(false),
    _resource_owner(resource_owner)
{
    size_t bucket_count = (index_count + 31) / 32;
    size_t width = std::max((size_t)1, std::min(bucket_count, (size_t)MAX_WIDTH));
    size_t height =
        std::max((size_t)1, (bucket_count == 0) ? 0 : ((bucket_count + width - 1) / width));
    assert(width * height >= bucket_count);

    _texture_size = lm::uvec2((uint32_t)width, (uint32_t)height);
    _bitmask.resize(width * height, 0);

    for (const auto& it : metadata)
    {
        _metadata.push_back({it.first, it.second});
    }
}

template<typename Indirection>
void SelectionStorageUint32Texture<Indirection>::update_selection(
    const hrz::flat_hash_set<uint64_t>& selected_objects)
{
    if (_texture_size.x == 0 || _texture_size.y == 0)
    {
        assert(false && "Uninitialized");
        return;
    }

    if (_any_selected || !selected_objects.empty())
    {
        std::ranges::fill(_bitmask, 0);
    }

    const bool had_any_selected = _any_selected;
    _any_selected = false;

    if (!selected_objects.empty())
    {
        for (auto object_id : selected_objects)
        {
            auto mark_index_as_selected = [&](uint32_t index)
            {
                const uint32_t bucket_index = index / 32;
                const uint32_t bit_index = index % 32;
                _bitmask[bucket_index] |= 1 << bit_index;
                _any_selected = true;
            };

            const FeatureIdIndirections indirections = _indirection.get_indirections(object_id);
            if (indirections.has_range())
            {
                for (auto index : indirections.range())
                {
                    mark_index_as_selected(index);
                }
            }
            else
            {
                mark_index_as_selected(indirections.single_index());
            }
        }
    }

    if (_any_selected || had_any_selected)
    {
        _need_to_update_texture = true;
    }
}

template<typename Indirection>
void SelectionStorageUint32Texture<Indirection>::work_gpu(Render* render)
{
    if (_texture_size.x == 0 || _texture_size.y == 0)
    {
        assert(false && "Uninitialized");
        return;
    }

    if (!_texture)
    {
        auto data = std::as_bytes(std::span<const uint32_t>(_bitmask));

        assert(_texture_size.x > 0 && _texture_size.y > 0);

        my::TextureResource res;
        res.layout.type = my::TextureLayout::Type2D;
        res.layout.format = my::TextureFormat::R32UI;
        res.layout.width = _texture_size.x;
        res.layout.height = _texture_size.y;
        res.layout.depth = 1;
        res.layout.levels = 1;
        res.data = {&data, 1};
        res.generate_mipmaps = false;

        _texture = render->rc->alloc(&res, _resource_owner, _metadata);
        render->rc->monitoring->register_gpu_resource_metadata(
            _texture, "selection storage"_ss, ""_ss);
    }

    if (_need_to_update_texture)
    {
        render->my->update_texture(
            _texture, my::TextureFormat::R32UI, 0, 0, 0, 0, _texture_size.x, _texture_size.y, 1,
            std::as_bytes(std::span<const uint32_t>(_bitmask)));
        _need_to_update_texture = false;
    }
}

template class SelectionStorageUint32Buffer<SelectionIndirectionSingleIndex>;
template class SelectionStorageUint32Buffer<SelectionIndirectionMultiIndex>;
template class SelectionStorageUint32Texture<SelectionIndirectionSingleIndex>;
template class SelectionStorageUint32Texture<SelectionIndirectionMultiIndex>;

} // namespace hrz::selection
