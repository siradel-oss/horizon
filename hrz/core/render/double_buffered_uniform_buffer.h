#pragma once

#include "hrz/common/metadata.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/core/clock.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/resources.h"
#include "hrz/fnd/array_view.h"

#include <lin_maths.h>
#include <mycelium/render_graph.h>
#include <mycelium/renderer.h>

#include <utility>

namespace hrz::render
{

template<typename T>
class DoubleBufferedUniformBuffer
{
    struct Buffer
    {
        int dirty_offset_begin = std::numeric_limits<int>::max();
        int dirty_offset_end = std::numeric_limits<int>::lowest();

        my::ResourceHandle handle;

        void invalidate_span(int begin, int end)
        {
            dirty_offset_begin = std::min(dirty_offset_begin, begin);
            dirty_offset_end = std::max(dirty_offset_end, end);
        }

        void reset_dirty_span()
        {
            dirty_offset_begin = std::numeric_limits<int>::max();
            dirty_offset_end = std::numeric_limits<int>::lowest();
        }

        constexpr bool has_dirty_span() const { return dirty_offset_begin < dirty_offset_end; }
    };

    int _count;
    int _stride = sizeof(T);
    std::unique_ptr<unsigned char[]> _data_raw;
    ArrayView<T> _data_view;
    Buffer _buffers[2];

    mutable int _current_writable = 0;
    mutable uint64_t _last_flip_frame = 0;

public:
    explicit DoubleBufferedUniformBuffer(int count = 1) : _count(count) {}

    void initialize(
        int count,
        hrz::Render* render,
        hrz::monitoring::systems::Name system,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata)
    {
        _count = count;
        initialize(render, system, metadata);
    }

    void initialize(
        hrz::Render* render,
        hrz::monitoring::systems::Name system,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata)
    {
        static constexpr size_t Size = sizeof(T);
        static constexpr size_t Alignment = alignof(T);

        _stride = _count > 1
            ? compute_ubo_stride<T>(render->my->get_uniform_buffer_offset_alignment())
            : Size;

        size_t unaligned_size = _stride * _count + Alignment - 1;
        _data_raw.reset(new unsigned char[unaligned_size]);

        void* unaligned_ptr = _data_raw.get();
        size_t aligned_size = unaligned_size;
        void* aligned_ptr = std::align(Alignment, Size, unaligned_ptr, aligned_size);
        assert(aligned_ptr);
        assert(aligned_size >= _stride * _count);
        _data_view = ArrayView<T>((T*)aligned_ptr, _count, _stride);

        for (size_t i = 0; i < _count; ++i)
        {
            new (&_data_view[i]) T{};
        }

        my::BufferResource res(my::BufferResource::BufferType::Uniform);
        res.size = _stride * _count;
        res.usage = my::UsageHint::Updatable;
        res.data = nullptr;

        _buffers[0].handle = render->rc->alloc(&res, system, metadata);
        _buffers[1].handle = render->rc->alloc(&res, system, metadata);
    }

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(_buffers[0].handle);
        rc->dealloc(_buffers[1].handle);
    }

    void destroy(hrz::Render* render) { destroy(render->rc); }

    void dirty_one(size_t i)
    {
        assert(i < _count);
        int begin = i * _stride;
        int end = begin + sizeof(T);
        _buffers[0].invalidate_span(begin, end);
        _buffers[1].invalidate_span(begin, end);
    }

    void dirty_all()
    {
        _buffers[0].invalidate_span(0, _stride * _count);
        _buffers[1].invalidate_span(0, _stride * _count);
    }

    void set(size_t i, const T& value)
    {
        assert(i < _count);
        _data_view[i] = value;
        dirty_one(i);
    }

    constexpr size_t offset(size_t i) const { return _stride * i; }

    const T& get(size_t i = 0) const
    {
        assert(i < _count);
        return _data_view[i];
    }

    T& get_mutable(size_t i = 0)
    {
        assert(i < _count);
        dirty_one(i);
        return _data_view[i];
    }

    bool update(my::RenderContext* r)
    {
        auto& buffer = _buffers[_current_writable];

        if (!buffer.has_dirty_span()) return false;

        r->update_buffer(
            buffer.handle, buffer.dirty_offset_begin,
            buffer.dirty_offset_end - buffer.dirty_offset_begin,
            (char*)_data_view.data() + buffer.dirty_offset_begin);

        buffer.reset_dirty_span();
        return true;
    }

    // Update must have been called before this!
    my::ResourceHandle get_for_gpu() const
    {
        if (_last_flip_frame != hrz::clock::CurrentFrameNumber)
        {
            _current_writable = 1 - _current_writable;
            _last_flip_frame = hrz::clock::CurrentFrameNumber;
        }

        const auto& buffer = _buffers[1 - _current_writable];
        assert(!buffer.has_dirty_span());
        return buffer.handle;
    }
};

} // namespace hrz::render
