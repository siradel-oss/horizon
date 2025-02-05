#pragma once

#include <gsl/gsl-lite.hpp>
#include <stdint.h>
#include <cstddef>

namespace {{ namespace }}
{
    enum class Resources
    {
        {% for key in keys %}
        {{ key }},
        {% endfor %}
    };

    gsl::span<const std::byte> get_data(Resources id);
}
