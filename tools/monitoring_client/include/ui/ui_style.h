#pragma once

#include <span>
#include <vector>

namespace ui::style
{

std::vector<const char*> get_names();
void apply(size_t index);

bool is_dark_mode();

} // namespace ui::style
