#pragma once

#include "absl/functional/function_ref.h"

namespace hrz
{

template<typename T>
using function_ref = absl::FunctionRef<T>;

} // namespace hrz
