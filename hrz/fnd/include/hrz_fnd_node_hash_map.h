#pragma once

#include "absl/container/node_hash_map.h"

namespace hrz
{
// An alternative to flat_hash_map when values are not movables or large. This
// is an for std::unordered_map, but prefer flat_hash_map when it's suitable.
// Warning: the usual `it = map.erase(it)` idiom is now `map.erase(it++)`.

using absl::node_hash_map;

} // namespace hrz
