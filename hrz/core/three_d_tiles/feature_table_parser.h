// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_array.h"
#include "hrz/common/three_d_tiles.h"
#include "hrz/fnd/class.h"
#include "hrz/fnd/json_utils.h"

#include <lin_maths.h>
#include <rapidjson/document.h>

#include <vector>

namespace hrz::three_d_tiles
{

class FeatureTableParser
{
    rapidjson::Document _doc;
    hrz::blobs::BlobHandle _bin_blob;
    hrz::blobs::BlobData _bin_data;
    std::span<const std::byte> _bin;

    FeatureTableParser(rapidjson::Document&& doc, const hrz::blobs::BlobHandle& bin_blob) :
        _doc(std::move(doc)),
        _bin_blob(bin_blob),
        _bin_data(_bin_blob.get_data()),
        _bin(_bin_data.as_bytes())
    {
    }

    template<typename T>
    std::optional<T> read_binary(size_t offset) const
    {
        if (offset + sizeof(T) <= _bin.size())
        {
            T result;
            std::memcpy(&result, _bin.data() + offset, sizeof(T));
            return result;
        }
        else
        {
            return std::nullopt;
        }
    }

    template<typename T>
    std::optional<T> read_binary(const rapidjson::Value& node) const
    {
        if (auto offset = json::get_uint(node, "byteOffset"); offset.has_value())
        {
            return read_binary<T>(offset.value());
        }
        else
        {
            return std::nullopt;
        }
    }

    template<typename T>
    std::optional<std::pair<size_t, size_t>> get_byte_offset_length(const char* name, size_t count)
        const
    {
        if (!has_semantic(name))
        {
            return std::nullopt;
        }

        const auto& node = _doc[name];
        if (auto offset = json::get_uint(node, "byteOffset"); offset.has_value())
        {
            const size_t byte_offset = offset.value();
            const size_t byte_length = count * sizeof(T);

            if (byte_offset + byte_length <= _bin.size())
            {
                return std::make_pair(byte_offset, byte_length);
            }
        }

        return std::nullopt;
    }

public:
    HRZ_DELETE_COPY(FeatureTableParser);
    HRZ_DEFAULT_MOVE(FeatureTableParser);
    ~FeatureTableParser() = default;

    inline bool has_semantic(const char* name) const { return _doc.HasMember(name); }

    static std::optional<FeatureTableParser> make(
        std::span<const std::byte> json_data,
        const hrz::blobs::BlobHandle& bin_blob)
    {
        rapidjson::Document document;
        document.Parse((const char*)json_data.data(), json_data.size());

        if (document.HasParseError() || !document.IsObject())
        {
            return std::nullopt;
        }

        return FeatureTableParser(std::move(document), bin_blob);
    }

    std::optional<bool> get_global_bool(const char* name) const
    {
        if (!has_semantic(name))
        {
            return std::nullopt;
        }

        return json::as_bool(_doc[name]);
    }

    std::optional<uint32_t> get_global_uint32(const char* name) const
    {
        if (!has_semantic(name))
        {
            return std::nullopt;
        }

        const auto& node = _doc[name];
        if (auto v = json::as_uint(node); v.has_value())
        {
            return v;
        }

        return read_binary<uint32_t>(node);
    }

    // In 3D Tiles, floating-point vectors are encoded as float32 in the binary data.
    // However they are often used for planet-scale coordinates, and since this specification
    // was written for the web, the values written are JSON are supposed to be deserialized as
    // doubles. It would have been better for the 3D Tiles spec to use float64 in the binary data,
    // but here we are...
    std::optional<lm::dvec3> get_global_dvec3(const char* name) const
    {
        if (!has_semantic(name))
        {
            return std::nullopt;
        }

        const auto& node = _doc[name];
        if (lm::dvec3 result; json::copy_array_values_fixed(result.m, node))
        {
            return result;
        }

        if (auto value = read_binary<lm::vec3>(node); value.has_value())
        {
            return lm::dvec3(*value);
        }

        return std::nullopt;
    }

    // Just in case, we do the same for vec4 (deserialize from JSON as double).
    std::optional<lm::dvec4> get_global_dvec4(const char* name) const
    {
        if (!has_semantic(name))
        {
            return std::nullopt;
        }

        const auto& node = _doc[name];
        if (lm::dvec4 result; json::copy_array_values_fixed(result.m, node))
        {
            return result;
        }

        if (auto value = read_binary<lm::vec4>(node); value.has_value())
        {
            return lm::dvec4(*value);
        }

        return std::nullopt;
    }

    template<typename T>
    std::optional<std::span<const T>> get_data_span(const char* name, size_t count) const
    {
        if (auto offset_length = get_byte_offset_length<T>(name, count); offset_length.has_value())
        {
            const size_t byte_offset = offset_length->first;
            const auto* data_ptr = (const T*)(_bin.data() + byte_offset);
            return std::span<const T>(data_ptr, count);
        }

        return std::nullopt;
    }

    template<typename T>
    std::optional<hrz::BlobArray<T>> get_blob_array(
        const char* name,
        size_t count,
        hrz::BlobAllocator* ba) const
    {
        if (auto offset_length = get_byte_offset_length<T>(name, count); offset_length.has_value())
        {
            const size_t byte_offset = offset_length->first;
            const size_t byte_length = offset_length->second;
            return hrz::BlobArray<T>::make_blob_array(
                ba,
                _bin_blob.make_sub_blob(
                    hrz::unsafe("Offset and size are checked above"), byte_offset, byte_length));
        }

        return std::nullopt;
    }

    template<typename T>
    bool copy_data(const char* name, size_t count, std::vector<T>& out) const
    {
        auto span = get_data_span<T>(name, count);
        if (!span.has_value())
        {
            return false;
        }

        out.resize(count);
        std::memcpy(out.data(), span->data(), count * sizeof(T));
        return true;
    }

    AttributeComponentType get_semantic_component_type(
        const char* name,
        AttributeComponentType default_type) const
    {
        if (!has_semantic(name))
        {
            return default_type;
        }

        const auto& node = _doc[name];
        if (auto type_str = json::get_str(node, "componentType"); type_str.has_value())
        {
            return component_type_from_string(type_str.value()).value_or(default_type);
        }

        return default_type;
    }
};

} // namespace hrz::three_d_tiles
