#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/common/vector_data/feature_id.h"
#include "hrz/common/vector_data/packed_attribute_values.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/protocol/attributes/feature_id.pb.h"

#include <lin_maths.h>

#include <cstdint>
#include <optional>

namespace hrz::vector_data
{

struct FeatureIds
{
    using Hash = uint64_t;

    // The array for the hashes needs to be allocated and have the
    // same size as the attribute values.
    // The data inside does not matter, the hashes are computed by
    // this function.
    // This function takes the ownership of the array.
    static std::optional<FeatureIds> make(
        std::span<AttributeValues> attribute_values,
        BlobArray<FeatureIdHash> hashes_array);

    Hash compute_hash();

    hrz::BlobArray<FeatureIdHash> hashes() const { return _hashes; }

    size_t size_bytes() const;

    size_t size() const { return _size; }

    bool empty() const { return _size == 0; }

    bool has_any_attribute() const { return !_values.empty(); }

    bool has_attribute(uint32_t attribute_id) const;
    bool has_same_attributes(const FeatureIds&) const;

    bool contains(const FeatureId& feature_id) const;
    FeatureId at(size_t i) const;

    void to_proto(google::protobuf::RepeatedPtrField<hrz_proto::FeatureId>* dst) const;

private:
    size_t _size = 0;
    hrz::InlinedVector<AttributeValues, 2> _values;
    hrz::BlobArray<FeatureIdHash> _hashes;
    std::optional<Hash> _hash;
};

} // namespace hrz::vector_data
