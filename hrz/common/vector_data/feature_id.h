#pragma once

#include "hrz/common/vector_data/attribute_type_owned.h"
#include "hrz/common/vector_data/feature_id_hash.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/protocol/attributes/feature_id.pb.h"

#include <lin_maths.h>

#include <cstdint>

namespace hrz::vector_data
{

struct FeatureIds;

struct FeatureId
{
    struct Value
    {
        uint32_t attribute_id;
        OwnedAttributeValue value;
    };

    struct Builder
    {
        void add_value(uint32_t attribute_id, OwnedAttributeValue);
        FeatureId build();

    private:
        hrz::InlinedVector<Value, 2> _values;
    };

    bool operator==(const FeatureId& other) const { return other._hash == _hash; }

    size_t value_count() const { return _values.size(); }

    Value value_at(size_t i) const { return _values.at(i); }

    bool is_null() const { return _values.empty(); }

    FeatureIdHash hash() const { return _hash; }

    static FeatureId from_proto(const hrz_proto::FeatureId&);
    void to_proto(hrz_proto::FeatureId* dst) const;

    template<typename H>
    friend H AbslHashValue(H h, const FeatureId& id)
    {
        return H::combine(std::move(h), id._hash);
    }

private:
    hrz::InlinedVector<Value, 2> _values;
    FeatureIdHash _hash;

    friend struct FeatureIds;
};

} // namespace hrz::vector_data
