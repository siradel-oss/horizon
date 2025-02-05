#pragma once

#include "hrz_jobs_protocol.h"

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz_jobs_proto
{
{% for e in protocol.enums %}
static const char* {{ e.full_name|to_short_type_name }}_Name({{ e.full_name|to_short_type_name }} value)
{
    switch (value)
    {
        {% for v in e["values"] %}
        case {{ e.full_name|to_short_type_name }}::{{ v.name|to_cpp_enum_value_name(e.full_name) }}: return "{{ v.name }}";
        {% endfor %}
        default: return "<Unknown>";
    }
}
{% endfor %}
}
