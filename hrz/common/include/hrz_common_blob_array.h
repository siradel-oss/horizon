#pragma once

#include "hrz_common_blob_allocator.h"
#include "hrz_common_metadata.h"

#include <hrz_fnd_unsafe.h>

#include <cassert>
#include <optional>
#include <span>
#include <type_traits>

namespace hrz
{
template<typename T>
class BlobArrayAllocation;

// An analogue to `std::array` that owns data stored in a blob.
//
// An instance of this class is always valid.
// The instance ownership can be transferred between systems.
// It isn't possible to append new data.
//
// Non trivially copyable types cannot be stored in a BlobArray
// because the data in the blob can be moved by the blob allocator
// without the BlobArray being aware of it.
template<typename T>
class BlobArray
{
    // Because the blob of a BlobArray can be relocated, non-
    // trivially copiable types cannot be stored without being
    // able to call move constructors on instances.
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(alignof(T) <= blobs::BLOB_ALIGNMENT, "Blob alignment incompatible");

public:
    BlobArray() : _blob({}), _size(0) {}

private:
    BlobArray(BlobAllocator* allocator, blobs::BlobHandle blob) :
        _blob(std::move(blob)), _size(_blob.data_size() / sizeof(T))
    {
        assert(_blob.is_valid());
        assert(_blob.check_integrity());
        assert(_blob.data_alignment() % alignof(T) == 0);

        register_blob_metadata(allocator, "type"_ss, "blob array"_ss);
    }

    BlobArray(blobs::BlobHandle blob) : _blob(std::move(blob)), _size(_blob.data_size() / sizeof(T))
    {
        assert(_blob.is_valid());
        assert(_blob.check_integrity());
        assert(_blob.data_alignment() % alignof(T) == 0);
    }

public:
    // Call this function where you cannot guarantee the alignment of the blob.
    static std::optional<BlobArray> make_blob_array(
        BlobAllocator* allocator,
        blobs::BlobHandle blob)
    {
        if (!blob.is_valid() || (blob.data_alignment() % alignof(T) != 0))
        {
            return std::nullopt;
        }

        return {BlobArray(allocator, std::move(blob))};
    }

    // @Safety Only call this if you can guarantee the alignment of the blob.
    static BlobArray make_blob_array(unsafe, BlobAllocator* allocator, blobs::BlobHandle blob)
    {
        return BlobArray(allocator, std::move(blob));
    }

    blobs::BlobHandle blob() const { return _blob; }

    size_t size() const { return _size; }

    size_t size_bytes() const { return _size * sizeof(T); }

    bool empty() const { return _size == 0; }

    void register_blob_metadata(
        BlobAllocator* allocator,
        MetadataString&& key,
        MetadataString&& value) const
    {
        if (_blob.is_valid())
        {
            blobs::register_metadata(allocator, _blob, std::move(key), std::move(value));
        }
    }

    void register_blob_owner(BlobAllocator* allocator, const monitoring::ResourceOwner& owner)
    {
        if (_blob.is_valid())
        {
            blobs::register_owner(allocator, _blob, owner);
        }
    }

    BlobArray make_sub_array(unsafe unsafe, size_t offset, size_t size) const
    {
        assert(offset + size <= _size);

        blobs::BlobHandle sub_blob =
            _blob.make_sub_blob(unsafe, offset * sizeof(T), size * sizeof(T));
        return BlobArray(std::move(sub_blob));
    }

    BlobArray make_sub_array(unsafe unsafe, size_t offset) const
    {
        return make_sub_array(unsafe, offset, _size - offset);
    }

    struct Data
    {
    public:
        Data(blobs::BlobData blob_data, size_t size) :
            _blob_data(std::move(blob_data)),
            _size(size),
            _data_view(std::span<const T>{(const T*)_blob_data->data(), _size})
        {
            assert(_blob_data->is_valid());
            assert(_blob_data->size() == sizeof(T) * _size);
        }

        Data() : _blob_data(std::nullopt), _size(0), _data_view() {}

        // The functions below that return pointers or references to blob data are marked as being
        // usable only on lvalues because we want to make sure the Data will outlive the returned
        // reference. If you don't have an lvalue and are sure that the lifetime will be respected,
        // you may use the unsafe variants of those methods. But be careful.

        constexpr size_t size() const { return _size; }

        constexpr size_t size_bytes() const { return sizeof(T) * _size; }

        constexpr bool empty() const { return _size == 0; }

        constexpr const T* data() & { return _data_view.data(); }

        constexpr std::span<const T> as_span() && = delete;

        constexpr std::span<const T> as_span() const& { return _data_view; }

        // Must outlive MutableData
        constexpr std::span<const T> unsafe_as_span() { return _data_view; }

        constexpr std::span<const std::byte> as_bytes() && = delete;

        constexpr std::span<const std::byte> as_bytes() const& { return std::as_bytes(_data_view); }

        typename std::span<const T>::iterator begin() & { return _data_view.begin(); }

        typename std::span<const T>::iterator end() & { return _data_view.end(); }

        typename std::span<const T>::iterator cbegin() && = delete;

        typename std::span<const T>::iterator cbegin() const& { return _data_view.begin(); }

        typename std::span<const T>::iterator cend() && = delete;

        typename std::span<const T>::iterator cend() const& { return _data_view.end(); }

        const T& at(size_t index) && = delete;

        const T& at(size_t index) const&
        {
            assert(index < _size);
            return _data_view[index];
        }

        const T& unsafe_at(size_t index) const
        {
            assert(index < _size);
            return _data_view[index];
        }

        const T& operator[](size_t index) && = delete;

        const T& operator[](size_t index) const& { return at(index); }

    private:
        std::optional<blobs::BlobData> _blob_data;
        size_t _size;
        std::span<const T> _data_view;
    };

    Data get_cdata() const
    {
        if (_blob.is_valid())
        {
            auto blob_data = _blob.get_data();
            auto data = Data(std::move(blob_data), _size);
            return std::move(data);
        }
        else
        {
            return Data();
        }
    }

    Data get_data() const { return get_cdata(); }

    struct MutableData
    {
    public:
        MutableData(blobs::MutableBlobData blob_data, size_t size) :
            _blob_data(std::move(blob_data)),
            _size(size),
            _data_view(std::span<T>{(T*)_blob_data->data(), _size})
        {
            assert(_blob_data->is_valid());
            assert(_blob_data->size() == sizeof(T) * _size);
        }

        MutableData() : _blob_data(std::nullopt), _size(0), _data_view() {}

        // For explanations about the lvalues here, see Data.

        constexpr size_t size() const { return _size; }

        constexpr size_t size_bytes() const { return sizeof(T) * _size; }

        constexpr bool empty() const { return _size == 0; }

        constexpr T* data() & { return _data_view.data(); }

        // Must outlive MutableData
        constexpr T* unsafe_data() { return _data_view.data(); }

        constexpr std::span<T> as_span() & { return _data_view; }

        // Must outlive MutableData
        constexpr std::span<T> unsafe_as_span() { return _data_view; }

        constexpr std::span<const std::byte> as_bytes() && = delete;

        constexpr std::span<const std::byte> as_bytes() const& { return std::as_bytes(_data_view); }

        constexpr std::span<std::byte> as_writable_bytes() &
        {
            return std::as_writable_bytes(_data_view);
        }

        typename std::span<T>::iterator begin() & { return _data_view.begin(); }

        typename std::span<T>::iterator end() & { return _data_view.end(); }

        typename std::span<const T>::iterator cbegin() && = delete;

        typename std::span<const T>::iterator cbegin() const& { return _data_view.begin(); }

        typename std::span<const T>::iterator cend() && = delete;

        typename std::span<const T>::iterator cend() const& { return _data_view.end(); }

        T& at(size_t index) &
        {
            assert(index < _size);
            return _data_view[index];
        }

        // Must outlive MutableData
        T& unsafe_at(size_t index)
        {
            assert(index < _size);
            return _data_view[index];
        }

        T& operator[](size_t index) & { return at(index); }

    private:
        std::optional<blobs::MutableBlobData> _blob_data;
        size_t _size;
        std::span<T> _data_view;
    };

    MutableData get_mutable_data()
    {
        if (_blob.is_valid())
        {
            auto blob_data = _blob.get_mutable_data();
            auto data = MutableData(std::move(blob_data), _size);
            return std::move(data);
        }
        else
        {
            return MutableData();
        }
    }

private:
    blobs::BlobHandle _blob;
    size_t _size;
};

enum class BlobArrayAllocationState
{
    NotAllocated,
    Allocated,
    Error,
};

template<typename T>
class BlobArrayAllocation
{
public:
    static BlobArrayAllocation allocate(BlobAllocator* blob_allocator, size_t size)
    {
        BlobArrayAllocation allocation;
        allocation._ticket = hrz::blobs::allocate_blob(blob_allocator, sizeof(T) * size);
        allocation._requested_size = size;
        return allocation;
    }

    BlobArrayAllocationState get_state(BlobAllocator* blob_allocator) const
    {
        if (!_ticket.is_valid())
        {
            return BlobArrayAllocationState::Error;
        }

        switch (blobs::get_state(blob_allocator, _ticket))
        {
            case blobs::BlobState::NotAllocated: return BlobArrayAllocationState::NotAllocated;
            case blobs::BlobState::Allocated: return BlobArrayAllocationState::Allocated;
            case blobs::BlobState::Error: return BlobArrayAllocationState::Error;
            case blobs::BlobState::InUse:
                assert(false && "Unexpected case");
                return BlobArrayAllocationState::Allocated;
            default: assert(false && "Unhandled case"); return BlobArrayAllocationState::Error;
        }
    }

    void register_blob_metadata(
        BlobAllocator* blob_allocator,
        MetadataString&& key,
        MetadataString&& value)
    {
        if (_ticket.is_valid())
        {
            blobs::register_metadata(blob_allocator, _ticket, std::move(key), std::move(value));
        }
    }

    void register_blob_owner(BlobAllocator* blob_allocator, const monitoring::ResourceOwner& owner)
    {
        if (_ticket.is_valid())
        {
            blobs::register_owner(blob_allocator, _ticket, owner);
        }
    }

    BlobArray<T> to_array(BlobAllocator* blob_allocator)
    {
        assert(
            _ticket.is_valid() && get_state(blob_allocator) == BlobArrayAllocationState::Allocated);
        if (get_state(blob_allocator) != BlobArrayAllocationState::Allocated)
        {
            return {};
        }

        auto blob = blobs::to_blob(blob_allocator, _ticket);

        return BlobArray<T>::make_blob_array(
            hrz::unsafe("The blob is a root blob"), blob_allocator, std::move(blob));
    }

    size_t requested_size() const { return _requested_size; }

    void cancel(BlobAllocator* blob_allocator)
    {
        blobs::cancel(blob_allocator, _ticket);
        _ticket = {};
    }

private:
    BlobArrayAllocation() = default;

    blobs::AllocationTicket _ticket;
    size_t _requested_size;
};
} // namespace hrz
