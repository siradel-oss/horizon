#pragma once

#include "hrz_common_blob_allocator.h"
#include "hrz_common_blob_array.h"
#include "hrz_fnd_array_view.h"

#include <gsl/gsl-lite.hpp>

#include <cassert>
#include <type_traits>

namespace hrz
{
// An analogue to `ArrayView` for data stored in a blob.
//
// Presently only non-mutating views are available.
template<typename T>
class BlobArrayView
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(alignof(T) <= blobs::BLOB_ALIGNMENT, "Blob alignment incompatible");

public:
    BlobArrayView() : _blob({}), _size(0), _offset(0), _stride(0) {}

    BlobArrayView(blobs::BlobHandle blob, size_t element_count, size_t offset, size_t stride) :
        _blob(std::move(blob)), _size(element_count), _offset(offset), _stride(stride)
    {
        assert(_blob.is_valid());
        assert(_blob.check_integrity());
    }

    explicit BlobArrayView(BlobArray<T> array) :
        _blob(std::move(array.blob())), _size(array.size()), _offset(0), _stride(sizeof(T))
    {
        assert(_blob.is_valid());
        assert(_blob.check_integrity());
    }

    blobs::BlobHandle blob() const { return _blob; }

    size_t size() const { return _size; }

    bool empty() const { return _size == 0; }

    struct DataView
    {
    public:
        DataView(blobs::BlobData blob_data, size_t size, size_t offset, size_t stride) :
            _blob_data(std::move(blob_data)),
            _size(size),
            _stride(stride),
            _data(_blob_data->data() + offset)
        {
            assert(_blob_data->is_valid());
            assert(_blob_data->size() <= size * stride + offset);
        }

        DataView() : _blob_data(std::nullopt), _size(0), _stride(0), _data(nullptr) {}

        // For explanations about the lvalues here, see BlobArray::Data.

        constexpr size_t size() const { return _size; }

        constexpr bool empty() const { return _size == 0; }

        constexpr const T* data() & { return _blob_data->data(); }

        constexpr ArrayView<const T> as_array_view() &
        {
            return ArrayView<const T>((const T*)_data, _size, _stride);
        }

        inline const T& at(size_t index) & { return *_at(index); }

        inline const T& operator[](size_t index) & { return *_at(index); }

    private:
        inline const T* _at(size_t index) const
        {
            assert(index < _size);
            return (const T*)(_data + index * _stride);
        }

        std::optional<blobs::BlobData> _blob_data;
        size_t _size;
        size_t _stride;
        const std::byte* _data;
    };

    DataView get_data_view() const
    {
        if (_blob.is_valid())
        {
            auto blob_data = _blob.get_data();
            auto data = DataView(std::move(blob_data), _size, _offset, _stride);
            return std::move(data);
        }
        else
        {
            return DataView();
        }
    }

private:
    blobs::BlobHandle _blob;
    size_t _size;
    size_t _offset;
    size_t _stride;
};
} // namespace hrz
