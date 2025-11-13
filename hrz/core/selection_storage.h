#pragma once

#include "hrz/common/monitoring_defs.h"
#include "hrz/core/render.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/flat_hash_set.h"

#include <lin_maths.h>
#include <mycelium/backend.h>

#include <stdint.h>
#include <string_view>
#include <utility>
#include <vector>

namespace hrz
{
struct GpuResourceContext;

namespace selection
{
/**
 * When implementing selection on a primitive, there arises the need to
 * identify which parts of the baked geometry are selected or not. This
 * identity often cannot be a simple ID because they might not be continuous
 * and we often need the storage of the selection to be dense is order to save
 * space.
 *
 * For example when displaying instanced models, we might use the instance ID
 * given by OpenGL to lookup the selection status of the current primitive.
 * Thus we need to be able to identify feature IDs by instance IDs in order to
 * update the selection status bitmap on CPU.
 *
 * There are two parts to this problem:
 *   - The storage strategy that stores the selection state for each index.
 *   - The indirection strategy that stores the id to index (or indices) indirection.
 */

struct FeatureIdIndirections
{
    struct Range
    {
        using Iterator = hrz::flat_hash_set<uint32_t>::const_iterator;

        Iterator _begin;
        Iterator _end;

        inline Iterator begin() const { return _begin; }

        inline Iterator end() const { return _end; }
    };

    using Variant = std::variant<uint32_t, Range>;
    Variant _variant;

    bool has_single_index() const
    {
        return _variant.index() == hrz::index_of_variant<Variant, uint32_t>();
    }

    uint32_t single_index() const { return std::get<uint32_t>(_variant); }

    bool has_range() const { return _variant.index() == hrz::index_of_variant<Variant, Range>(); }

    const Range& range() const { return std::get<Range>(_variant); }
};

class SelectionIndirectionSingleIndex
{
private:
    hrz::flat_hash_map<uint64_t, uint32_t> _id_to_indices;

public:
    explicit SelectionIndirectionSingleIndex(size_t index_count = 0)
    {
        _id_to_indices.reserve(index_count);
    }

    FeatureIdIndirections get_indirections(uint64_t feature_id)
    {
        auto it = _id_to_indices.find(feature_id);
        return {it->second};
    }

    inline void register_indirection(uint64_t id, uint32_t index)
    {
        _id_to_indices.insert(std::make_pair(id, index));
    }
};

class SelectionIndirectionMultiIndex
{
private:
    hrz::flat_hash_map<uint64_t, hrz::flat_hash_set<uint32_t>> _id_to_indices;

public:
    explicit SelectionIndirectionMultiIndex(size_t index_count = 0)
    {
        _id_to_indices.reserve(index_count);
    }

    FeatureIdIndirections get_indirections(uint64_t feature_id)
    {
        auto it = _id_to_indices.find(feature_id);
        if (it == _id_to_indices.end())
        {
            return {FeatureIdIndirections::Range{{}, {}}};
        }

        return {FeatureIdIndirections::Range{it->second.begin(), it->second.end()}};
    }

    inline void register_indirection(uint64_t id, uint32_t index)
    {
        auto& set = _id_to_indices[id];
        set.insert(index);
    }
};

/**
 * Stores the selection state in a buffer of uint32s where each bit is an index.
 * The selection can be retrieved with the (index % 32)th bit of buffer[index / 32].
 */
template<typename Indirection>
class SelectionStorageUint32Buffer
{
    Indirection _indirection;
    std::vector<uint32_t> _bitmap;
    my::ResourceHandle _buffer;
    bool _any_selected;
    bool _need_to_update_buffer;

    monitoring::ResourceOwner resource_owner;
    std::vector<std::pair<MetadataString, MetadataString>> _metadata;

public:
    SelectionStorageUint32Buffer() = default;
    SelectionStorageUint32Buffer(
        size_t index_count,
        const monitoring::ResourceOwner& resource_owner,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata);

    SelectionStorageUint32Buffer(SelectionStorageUint32Buffer&&) = default;
    SelectionStorageUint32Buffer& operator=(SelectionStorageUint32Buffer&&) = default;

    inline void free_gpu_resources(std::vector<my::ResourceHandle>& to_destroy)
    {
        if (!_buffer.is_null())
        {
            to_destroy.push_back(_buffer);
            _buffer = my::ResourceHandle::null();
        }
    }

    inline my::VertexInputStream get_vertex_input_stream(Render* render, int index)
    {
        if (!_buffer) work_gpu(render);
        return {index, _buffer, my::VertexFormat::UInt32, 0, 0, my::VertexRate::Per32Instances};
    }

    inline bool has_any_selected() const { return _any_selected; }

    inline void register_indirection(uint64_t id, uint32_t index)
    {
        _indirection.register_indirection(id, index);
    }

    void update_selection(const hrz::flat_hash_set<uint64_t>& selected_objects);
    void work_gpu(Render*);
};

/**
 * Stores the selection state in a texture of uint32s where each bit is an index.
 * The selection can be retrieved with the (index % 32)th bit of the
 * ((index / 32) % MAX_WIDTH, (index / 32) / MAX_WIDTH) pixel.
 */
template<typename Indirection>
class SelectionStorageUint32Texture
{
    static const uint32_t MAX_WIDTH = 2048;

    Indirection _indirection;
    my::ResourceHandle _texture;
    lm::uvec2 _texture_size{0, 0};
    std::vector<uint32_t> _bitmask;
    bool _need_to_update_texture = false;
    bool _any_selected = false;

    monitoring::ResourceOwner _resource_owner;
    std::vector<std::pair<MetadataString, MetadataString>> _metadata;

public:
    SelectionStorageUint32Texture() = default;
    SelectionStorageUint32Texture(
        size_t index_count,
        const monitoring::ResourceOwner& resource_owner,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata);

    SelectionStorageUint32Texture(SelectionStorageUint32Texture&&) = default;
    SelectionStorageUint32Texture& operator=(SelectionStorageUint32Texture&&) = default;

    inline void free_gpu_resources(std::vector<my::ResourceHandle>& to_destroy)
    {
        if (!_texture.is_null())
        {
            to_destroy.push_back(_texture);
            _texture = my::ResourceHandle::null();
        }
    }

    constexpr my::ResourceHandle get_texture(Render* render)
    {
        if (!_texture) work_gpu(render);
        return _texture;
    }

    constexpr bool has_any_selected() const { return _any_selected; }

    inline void register_indirection(uint64_t id, uint32_t index)
    {
        _indirection.register_indirection(id, index);
    }

    void update_selection(const hrz::flat_hash_set<uint64_t>& selected_objects);
    void work_gpu(Render*);
};

extern template class SelectionStorageUint32Buffer<SelectionIndirectionSingleIndex>;
extern template class SelectionStorageUint32Buffer<SelectionIndirectionMultiIndex>;
extern template class SelectionStorageUint32Texture<SelectionIndirectionSingleIndex>;
extern template class SelectionStorageUint32Texture<SelectionIndirectionMultiIndex>;

using SelectionStorageUint32BufferSingleIndex =
    SelectionStorageUint32Buffer<SelectionIndirectionSingleIndex>;
using SelectionStorageUint32TextureSingleIndex =
    SelectionStorageUint32Texture<SelectionIndirectionSingleIndex>;
using SelectionStorageUint32BufferMultiIndex =
    SelectionStorageUint32Buffer<SelectionIndirectionMultiIndex>;
using SelectionStorageUint32TextureMultiIndex =
    SelectionStorageUint32Texture<SelectionIndirectionMultiIndex>;

} // namespace selection
} // namespace hrz
