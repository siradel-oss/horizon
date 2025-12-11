#pragma once

#include "hrz/common/blob_array_view.h"

#include <lin_maths.h>

namespace hrz::planet
{

using ElevationQueryPointStorage = std::variant<hrz::BlobArrayView<lm::dvec2>, lm::dvec2>;

} // namespace hrz::planet
