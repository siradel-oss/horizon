#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/common/vector_data/attribute_type_packed.h"
#include "hrz/common/vector_data/attribute_type_ref.h"

namespace hrz::vector_data
{

struct PackedAttributeValuesReader
{
    hrz::BlobArray<PackedAttributeValue>::Data values;
    hrz::BlobArray<char>::Data ool_data;

    inline size_t size() const { return values.size(); }

    inline bool as_bool(size_t i) const { return attr_as_bool(values[i], ool_data.as_span()); }

    inline int64_t as_int64(size_t i) const { return attr_as_int64(values[i], ool_data.as_span()); }

    inline uint64_t as_uint64(size_t i) const
    {
        return attr_as_uint64(values[i], ool_data.as_span());
    }

    inline double as_number(size_t i) const
    {
        return attr_as_number(values[i], ool_data.as_span());
    }

    inline std::string_view as_string(size_t i) const
    {
        return attr_as_string(values[i], ool_data.as_span());
    }

    inline lm::ubvec4 as_color(size_t i) const
    {
        return attr_as_color(values[i], ool_data.as_span());
    }

    inline RefAttributeValue as_ref(size_t i) const
    {
        return attr_as_ref(values[i], ool_data.as_span());
    }
};

struct AttributeValues
{
    PackedAttributeValuesReader get_reader() const
    {
        return {values.get_cdata(), out_of_line_data.get_cdata()};
    }

    uint32_t attribute_id{};

    hrz::BlobArray<PackedAttributeValue> values;

    // This is the blob of all packed strings and uint64s.
    hrz::BlobArray<char> out_of_line_data;
};

} // namespace hrz::vector_data
