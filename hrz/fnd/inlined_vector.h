#pragma once

#include "absl/container/inlined_vector.h"

namespace hrz
{
// An "inlined vector" behaves in an equivalent fashion to a `std::vector`,
// except that storage for small sequences of the vector are provided inline
// without requiring any heap allocation.

using absl::InlinedVector;

} // namespace hrz
