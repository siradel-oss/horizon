#pragma once

#include "absl/container/flat_hash_set.h"

namespace hrz
{
// Hash set that is good when the values are small and copyable. For example
// integer, strings, etc.
// Warning: the usual `it = set.erase(it)` idiom is now `set.erase(it++)`.

using absl::flat_hash_set;

} // namespace hrz
