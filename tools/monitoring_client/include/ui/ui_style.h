#pragma once

#include <gsl/gsl-lite.hpp>

#include <vector>

namespace ui::style
{
std::vector<const char*> get_names();
void apply(size_t index);

bool is_dark_mode();
} // namespace ui::style
