// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_vector.h"
#include "hrz/common/vector_data/attribute_type_owned.h"
#include "hrz/common/vector_data/attribute_type_packed.h"
#include "hrz/common/vector_data/attribute_type_ref.h"
#include "hrz/common/vector_data/attributes_ops.h"
#include "hrz/common/vector_data/packed_attribute_values.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/protocol/attributes/transform.pb.h"

#include <rapidjson/fwd.h>

namespace hrz::vector_data
{

class PackedAttributeValuesEncoder
{
    hrz::BlobVector<char> _out_of_line_data;

    hrz::flat_hash_map<uint64_t, PackedAttributeValue> _encoded_uint64s;
    hrz::flat_hash_map<int64_t, PackedAttributeValue> _encoded_int64s;
    hrz::flat_hash_map<std::string, PackedAttributeValue> _encoded_strings;

public:
    PackedAttributeValuesEncoder(
        BlobAllocator* allocator,
        const monitoring::ResourceOwner& blob_owner) :
        _out_of_line_data(hrz::BlobVector<char>(allocator))
    {
        _out_of_line_data.register_blob_metadata(
            "contents"_ss, "attribute values out of line data"_ss);
        _out_of_line_data.register_blob_owner(blob_owner);
    }

    inline PackedAttributeValue encode(bool value)
    {
        return PackedAttributeValueTraits<>::from_bool(value);
    }

    PackedAttributeValue encode(uint64_t value);
    PackedAttributeValue encode(int64_t value);

    inline PackedAttributeValue encode(double value)
    {
        return PackedAttributeValueTraits<>::from_number(value);
    }

    PackedAttributeValue encode(std::string_view value);

    PackedAttributeValue encode(const char* value) { return encode(std::string_view(value)); }

    template<int N>
    PackedAttributeValue encode(const char (&value)[N])
    {
        return encode(std::string_view(value, N - 1));
    }

    PackedAttributeValue encode_ref(RefAttributeValue value)
    {
        switch (attr_type(value))
        {
            case AttributeValueType::kNull: return attr_null<PackedAttributeValue>();
            case AttributeValueType::kBoolean: return encode(attr_get_bool(value));
            case AttributeValueType::kNumber: return encode(attr_get_number(value));
            case AttributeValueType::kUint64: return encode(attr_get_uint64(value));
            case AttributeValueType::kInt64: return encode(attr_get_int64(value));
            case AttributeValueType::kString: return encode(attr_get_string(value));
            default: assert(false && "Unhandled case"); return attr_null<PackedAttributeValue>();
        }
    }

    std::optional<std::span<const char>> get_out_of_line_data() const
    {
        return _out_of_line_data.data();
    }

    std::optional<hrz::BlobArray<char>> finalize();
};

class PackedAttributeValuesBuilder
{
    hrz::BlobVector<vector_data::PackedAttributeValue> _values;
    PackedAttributeValuesEncoder _encoder;

public:
    PackedAttributeValuesBuilder(
        size_t initial_capacity,
        BlobAllocator* allocator,
        const monitoring::ResourceOwner& blob_owner) :
        _values(hrz::BlobVector<vector_data::PackedAttributeValue>(allocator, initial_capacity)),
        _encoder(allocator, blob_owner)
    {
        _values.register_blob_metadata("contents"_ss, "attribute values values"_ss);
        _values.register_blob_owner(blob_owner);
    }

    // This can be used to intern values that are known to be repeated.
    constexpr PackedAttributeValuesEncoder& encoder() { return _encoder; }

    inline void reserve(size_t capacity) { _values.reserve(capacity); }

    inline void resize(size_t size) { _values.resize(size); }

    template<typename T>
    void push(T&& value)
    {
        _values.push_back(_encoder.encode(std::forward<T>(value)));
    }

    // Returns whether the value type was handled. In any case a value is inserted.
    bool push_json(hrz_proto::AttributeTransform transform, const rapidjson::Value& value);

    void push_ref(const RefAttributeValue& value) { _values.push_back(_encoder.encode_ref(value)); }

    void push_transform(hrz_proto::AttributeTransform transform, const RefAttributeValue& value)
    {
        if (transform == hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_NONE)
        {
            push_ref(value);
        }
        else
        {
            auto value_transformed = attr_transform<OwnedAttributeValue>(transform, value);
            push_ref(attr_as_ref(value_transformed));
        }
    }

    // @Safety: the value must have been encoded using the encoder of this builder.
    void push_encoded(unsafe, PackedAttributeValue value) { _values.push_back(value); }

    void push_null() { _values.push_back(attr_null<PackedAttributeValue>()); }

    template<typename T>
    inline void set(size_t i, T&& value)
    {
        auto values_data = _values.data();
        if (values_data.has_value())
        {
            values_data.value()[i] = _encoder.encode(std::forward<T>(value));
        }
    }

    inline void set_ref(size_t i, const RefAttributeValue& value)
    {
        auto values_data = _values.data();
        if (values_data.has_value())
        {
            values_data.value()[i] = _encoder.encode_ref(value);
        }
    }

    std::optional<hrz::vector_data::AttributeValues> finalize(uint32_t attribute_id);
};

} // namespace hrz::vector_data
