// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <span>

namespace {{ namespace }}
{
    enum class Resources
    {
        {% for key in keys %}
        {{ key }},
        {% endfor %}
    };

    std::span<const std::byte> get_data(Resources id);
}
