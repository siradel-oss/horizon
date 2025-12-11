#pragma once

#include "hrz/common/vector_data/attribute_types_defs.h"

namespace hrz::vector_data
{

using RefAttributeValue = typename RefAttributeValueTraits::Type;

template<>
struct AttributeValueToTraits<typename RefAttributeValueTraits::Type>
{
    using Traits = RefAttributeValueTraits;
};

} // namespace hrz::vector_data
