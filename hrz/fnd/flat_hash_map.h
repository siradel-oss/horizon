// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "absl/container/flat_hash_map.h"

namespace hrz
{

// Hash map that is good when the keys are small and copyable. For example
// integer, strings, etc. The values must be movable. If the value is large,
// prefer std::unique_ptr<Value>, or even a node_hash_map.
// Warning: the usual `it = map.erase(it)` idiom is now `map.erase(it++)`.

using absl::flat_hash_map;

} // namespace hrz
