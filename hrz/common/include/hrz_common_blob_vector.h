#pragma once

#include "hrz_common_blob_allocator.h"
#include "hrz_common_blob_array.h"
#include "hrz_common_metadata.h"

#include <cassert>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace hrz
{
// An analogue to `std::vector` that allocates its data in blobs.
//
// Because an instance of this class always keeps an access to the
// data in the blob, it is non-copyable. Use this class when con-
// catenating data in memory, but as soon as this operation is
// done, convert the instance to a `BlobArray`, which can be
// passed to other systems.
//
// The data is stored in a blob. One is allocated at construction,
// and if necessary, another blob can be allocated when pushing
// new data into the vector, if it is full. Blob allocations are
// synchronous (i.e. blocking), so only use this class in jobs.
// Blob allocations can fail, when that happens, the instance is
// no longer valid. In order to have a simple API, it is allowed
// to call `push_back()` whether or not the instance is valid.
// However, it is required to check the validity of the instance
// before trying to read data, or converting the instance to a
// `BlobArray`.
template<typename T>
class BlobVector
{
    static_assert(
        std::is_trivially_destructible_v<T>,
        "BlobVector only supports trivially destructible types");
    static_assert(alignof(T) <= blobs::BLOB_ALIGNMENT, "Blob alignment incompatible");

private:
    // See https://github.com/facebook/folly/blob/main/folly/docs/FBVector.md#memory-handling
    static constexpr size_t SmallBlobGrowthFactor = 2;
    static constexpr float BigBlobGrowthFactor = 1.5f;
    static constexpr size_t BigBlobCutoffSize = 1024 * 1024; // 1 MiB
    static constexpr size_t DefaultCapacity = 128;

public:
    BlobVector(BlobAllocator* allocator, size_t initial_capacity = DefaultCapacity) :
        _allocator(allocator)
    {
        assert(allocator);
        allocate_new_blob(initial_capacity);
    }

    bool is_valid() const { return _blob.is_valid(); }

    size_t capacity() const { return _capacity; }

    std::optional<size_t> size() const
    {
        if (!is_valid()) return std::nullopt;
        return _size;
    }

    std::optional<bool> empty() const
    {
        if (!is_valid()) return std::nullopt;
        return _size == 0;
    }

    void reserve(size_t new_capacity)
    {
        if (!is_valid() || new_capacity == _capacity) return;

        if (new_capacity > _capacity)
        {
            reallocate_to_new_blob(new_capacity);
        }
        else
        {
            _blob_data = std::nullopt;
            blobs::shrink_blob(_allocator, _blob, sizeof(T) * new_capacity);
            _capacity = new_capacity;
            _size = std::min(_size, _capacity);
            _blob_data = std::optional<blobs::MutableBlobData>(std::move(_blob.get_mutable_data()));
        }
    }

    void resize(size_t new_size)
    {
        if (new_size == 0)
        {
            clear();
            return;
        }
        else if (new_size <= _size)
        {
            _size = new_size;
            return;
        }

        if (!is_valid()) return;

        if (new_size > _capacity)
        {
            if (!grow(new_size)) return;
        }

        assert(_capacity >= new_size);

        T* to_init_begin = (T*)(_blob_data.value().data() + sizeof(T) * _size);
        T* to_init_end = to_init_begin + new_size - _size;

        if constexpr (std::is_trivially_default_constructible_v<T>)
        {
            std::memset(to_init_begin, 0, sizeof(T) * (new_size - _size));
        }
        else
        {
            for (T* ptr = to_init_begin; ptr < to_init_end; ++ptr)
            {
                new (ptr) T{};
            }
        }

        _size = new_size;
        assert(_blob.check_integrity());
    }

    void push_back(T value)
    {
        if (!is_valid()) return;

        assert(_blob.is_valid());
        assert(_blob_data.has_value());

        if (_size == _capacity)
        {
            if (!grow(_capacity + 1)) return;
        }
        assert(_capacity > _size);

        T* ptr = (T*)(_blob_data.value().data() + sizeof(T) * _size);
        new (ptr) T{};
        *ptr = std::move(value);
        _size += 1;
    }

    // Returns nullptr if the vector isn't valid or if the index is out
    // of bounds.
    // This is not `std::optional<T&> at(size_t index)` because optionals
    // cannot contain references.
    T* at(size_t index)
    {
        if (!is_valid()) return nullptr;

        if (index >= _size) return nullptr;

        return (T*)(_blob_data.value().data() + sizeof(T) * index);
    }

    const T* at(size_t index) const
    {
        if (!is_valid()) return nullptr;

        if (index >= _size) return nullptr;

        return (const T*)(_blob_data.value().data() + sizeof(T) * index);
    }

    // Returns nullopt if the vector isn't valid or if the index is out
    // of bounds.
    // Returns an optional to a value, not a reference.
    std::optional<T> get_value(size_t index) const
    {
        if (!is_valid()) return std::nullopt;

        if (index >= _size) return std::nullopt;

        auto* ptr = (const T*)(_blob_data.value().data() + sizeof(T) * index);
        return {*ptr};
    }

    void clear()
    {
        if (is_valid())
        {
            _size = 0;
        }
        else
        {
            allocate_new_blob(0);
        }
    }

    void register_blob_metadata(MetadataString&& key, MetadataString&& value)
    {
        if (_blob.is_valid())
        {
            blobs::register_metadata(_allocator, _blob, std::move(key), std::move(value));
        }
    }

    void register_blob_owner(const monitoring::ResourceOwner& owner)
    {
        if (_blob.is_valid())
        {
            blobs::register_owner(_allocator, _blob, owner);
        }
    }

    std::optional<std::span<T>> data()
    {
        if (!is_valid()) return std::nullopt;
        if (_size == 0) return {std::span<T>{}};
        return {{(T*)_blob_data->data(), _size}};
    }

    std::optional<std::span<const T>> data() const
    {
        if (!is_valid()) return std::nullopt;
        if (_size == 0) return {std::span<const T>{}};
        return {{(const T*)_blob_data->data(), _size}};
    }

    // Consumes the instance.
    std::optional<BlobArray<T>> to_blob_array()
    {
        if (!is_valid())
        {
            return std::nullopt;
        }

        _blob_data = std::nullopt;

        std::optional<BlobArray<T>> array = std::nullopt;

        if (_size > 0)
        {
            blobs::shrink_blob(_allocator, _blob, sizeof(T) * _size);

            array = {BlobArray<T>::make_blob_array(
                hrz::unsafe("The blob is a root blob"), _allocator, std::move(_blob))};
        }
        else
        {
            array = {BlobArray<T>()};
        }

        _blob = {};
        _capacity = 0;
        _size = 0;

        return std::move(array);
    }

private:
    BlobAllocator* _allocator;

    blobs::BlobHandle _blob;
    std::optional<blobs::MutableBlobData> _blob_data;
    size_t _capacity{};
    size_t _size{};

    void allocate_new_blob(size_t capacity)
    {
        auto new_blob_opt = blobs::allocate_blob_sync(_allocator, sizeof(T) * capacity);
        if (new_blob_opt.has_value())
        {
            _blob = std::move(new_blob_opt.value());
            _blob_data = std::move(
                std::optional<blobs::MutableBlobData>(std::move(_blob.get_mutable_data())));
            _capacity = capacity;

            register_blob_metadata("type"_ss, "blob vector"_ss);
        }
    }

    bool reallocate_to_new_blob(size_t new_capacity)
    {
        auto new_blob_opt = blobs::allocate_blob_sync(_allocator, sizeof(T) * new_capacity);
        if (!new_blob_opt.has_value())
        {
            _blob_data = std::nullopt;
            _blob = {};
            _capacity = 0;
            _size = 0;
            return false;
        }

        blobs::copy_metadata(_allocator, _blob, new_blob_opt.value());
        blobs::copy_owner(_allocator, _blob, new_blob_opt.value());

        auto new_blob_data = new_blob_opt.value().get_mutable_data();

        if (_blob_data.has_value())
        {
            if (std::is_trivially_copyable_v<T>)
            {
                std::memcpy(new_blob_data.data(), _blob_data->data(), sizeof(T) * _size);
            }
            else
            {
                auto from = std::span<T>{(T*)_blob_data->data(), _size};
                auto to = std::span<T>{(T*)new_blob_data.data(), _size};
                for (size_t i = 0; i < _size; ++i)
                {
                    new (&to[i]) T{};
                    to[i] = std::move(from[i]);
                }
            }
        }
        else
        {
            assert(_size == 0);
        }

        _blob = std::move(new_blob_opt.value());
        _blob_data = std::move(new_blob_data);
        _capacity = new_capacity;

        return true;
    }

    bool grow(size_t target_capacity)
    {
        size_t next_capacity = _capacity;
        if (_capacity >= BigBlobCutoffSize)
        {
            next_capacity *= BigBlobGrowthFactor;
        }
        else
        {
            next_capacity *= SmallBlobGrowthFactor;
        }

        size_t new_capacity = target_capacity <= next_capacity ? next_capacity : target_capacity;
        if (reallocate_to_new_blob(new_capacity))
        {
            assert(new_capacity >= target_capacity);
            assert(_blob.check_integrity());
            return true;
        }
        else
        {
            return false;
        }
    }
};
} // namespace hrz
