// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace hrz::style
{

struct StyledFeatureInstance
{
    uint32_t feature_index;
    uint32_t repr_id;
    uint32_t first_prp; // Index in "prps" and "values"
    uint32_t prp_count;
};

} // namespace hrz::style
