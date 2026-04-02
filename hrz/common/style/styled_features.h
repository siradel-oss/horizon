#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/common/style/styled_feature_instance.h"
#include "hrz/common/vector_data/packed_attribute_values.h"

#include <cstdint>

namespace hrz::style
{

struct StyledFeatures
{
    using Instance = StyledFeatureInstance;

    BlobArray<Instance> instances;

    // All properties and their values are packed here. This is indexed by "first_prp"
    // and "prp_count" from each instance.
    // @Todo @Memory: An indirection array from interned property ID to a shorter ID could be used
    // to reduce the size of the IDs in this array.
    BlobArray<uint64_t> prps;
    BlobArray<vector_data::PackedAttributeValue> values;

    // All string packed here. Just like attributes.
    BlobArray<char> out_of_line_data;

    inline vector_data::PackedAttributeValuesReader get_values_reader() const
    {
        return vector_data::PackedAttributeValuesReader{
            values.get_cdata(), out_of_line_data.get_cdata()
        };
    }
};

} // namespace hrz::style
