// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

namespace hrz::migration
{

struct DynamicMessage;

// Migrate the content of src to dst. The version field will be automatically
// filled. Return true on success, false otherwise.
using MigrationFn = bool (*)(const DynamicMessage& src, DynamicMessage* dst);

struct MigrationEntry
{
    uint32_t id;
    MigrationFn function;
};

{% for id in migration_ids -%}
{% if not loop.last -%}
bool migration_{{ id }}_to_{{ loop.nextitem }}(const DynamicMessage& src, DynamicMessage* dst);
{% endif -%}
{%- endfor %}

static const MigrationEntry Migrations[] = {
{%- for id in migration_ids %}
    {
        0x{{ id }}u,
        {% if not loop.last -%}
            migration_{{ id }}_to_{{ loop.nextitem }}
        {%- else -%}
            nullptr
        {%- endif %}
    }{{ "," if not loop.last }}
{%- endfor %}
};

static constexpr size_t MigrationCount = {{ migration_ids | count }};

} // namespace hrz
