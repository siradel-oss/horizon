#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_array.h"
#include "hrz/common/metadata.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/fnd/meta.h"

#include <lin_maths.h>
#include <mycelium/backend.h>

#include <utility>

namespace hrz
{
struct Render;
} // namespace hrz

namespace hrz::render
{

// Builds a vertex input resource and its associated vertex buffer by appending blobs of data.
// The resulting layout is an SoA.
struct VertexInputBuilder
{
    struct ToUpload
    {
        const void* data;
        size_t offset;
        size_t size;
    };

    size_t full_size = 0;

    // Necessary to not move the blob data before the upload, since a pointer to the data is kept in
    // the to_upload array.
    hrz::InlinedVector<blobs::BlobData, 8> pinned_blobs;

    hrz::InlinedVector<ToUpload, 8> to_upload;
    hrz::InlinedVector<my::VertexInputStream, 8> vertex_input_streams;

    void add_input_stream_raw(
        int index,
        std::span<const std::byte> data,
        my::VertexFormat format,
        my::VertexRate rate);

    void add_input_stream(
        int index,
        blobs::BlobHandle blob,
        my::VertexFormat format,
        my::VertexRate rate);

    template<typename T>
    inline void add_input_stream(
        int index,
        const T& v,
        my::VertexFormat format,
        my::VertexRate rate = my::VertexRate::Constant)
    {
        add_input_stream_raw(index, std::as_bytes(std::span<const T>(&v, 1)), format, rate);
    }

    template<typename T>
    inline void add_input_stream(
        int index,
        std::span<const T> data,
        my::VertexFormat format,
        my::VertexRate rate)
    {
        add_input_stream_raw(index, std::as_bytes(data), format, rate);
    }

    template<typename T>
    void add_input_stream(
        int index,
        const BlobArray<T>& data,
        my::VertexFormat format,
        my::VertexRate rate)
    {
        add_input_stream(index, data.blob(), format, rate);
    }

    template<typename T>
    void add_input_stream(
        int index,
        const std::variant<BlobArray<T>, T>& data,
        my::VertexFormat format,
        my::VertexRate rate)
    {
        std::visit(
            hrz::overload{
                [this, index, format, rate](const BlobArray<T>& arg)
                { add_input_stream(index, arg, format, rate); },
                [this, index, format, rate](const T& arg)
                { add_input_stream(index, std::span<const T>(&arg, 1), format, rate); }},
            data);
    }

    template<typename T>
    void add_input_stream(
        int index,
        const std::variant<blobs::BlobHandle, T>& data,
        my::VertexFormat format,
        my::VertexRate rate)
    {
        std::visit(
            hrz::overload{
                [this, index, format, rate](const blobs::BlobHandle& arg)
                { add_input_stream(index, arg, format, rate); },
                [this, index, format, rate](const T& arg)
                { add_input_stream(index, std::span<const T>(&arg, 1), format, rate); }},
            data);
    }

    // First is the buffer, second is the vertex input
    std::pair<my::ResourceHandle, my::ResourceHandle> build(
        hrz::Render* render,
        monitoring::systems::Name system,
        uint64_t layer_id = 0,
        std::initializer_list<std::pair<MetadataString, MetadataString>> metadata = {});
};

} // namespace hrz::render
