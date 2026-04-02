////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "hrz/core/style/enums.h"

namespace hrz::style
{
EnumFindResult find_enum_value_by_name(std::string_view enum_name, std::string_view value_name)
{
    EnumFindResult res;
    res.type = EnumFindResult::Type::Ok;
    res.value = 0;

{% for e in protocol.enums %}
{% if e["expose_to_style"] %}
    // {{ e.full_name }}
    if (enum_name == "{{ e.name }}")
    {
{% for v in e["values"] %}
        if (value_name == "{{ v.name }}")
        {
            res.value = {{ v.id }};
            return res;
        }
{% endfor %}

        res.type = EnumFindResult::Type::UnknownValue;
        return res;
    }

{% endif %}
{% endfor %}

    res.type = EnumFindResult::Type::UnknownEnum;
    return res;
}
}
