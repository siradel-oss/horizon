// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/vector_data/attribute_types_defs.h"
#include "hrz/fnd/arena.h"

namespace hrz::vector_data
{

struct RefAttributeValueArenaTraits : public RefAttributeValueTraitsGeneric<hrz::Arena>
{
    static inline Type from_string(std::string_view value, hrz::Arena& arena)
    {
        return arena.str(value);
    }
};

} // namespace hrz::vector_data
