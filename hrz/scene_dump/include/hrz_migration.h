#pragma once

#include "dynamic_message.h"

#include <gsl/gsl-lite.hpp>

#include <vector>

namespace hrz::migration
{

// In case of error, returns an empty vector.
// applied returns the number of migrations that have been applied.
std::vector<std::byte> migrate(std::vector<std::byte> data, int* applied = nullptr);

} // namespace hrz::migration
