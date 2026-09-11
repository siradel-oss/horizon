// SPDX-FileCopyrightText: Copyright 2018 Siradel
// SPDX-License-Identifier: MIT

#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace HrzProtocol
{
{% for s in services %}
{% for m in s.methods %}
class {{ m.input|last(".") }};
class {{ m.output|last(".") }};
{% endfor %}
{% endfor %}
} // namespace HrzProtocol

namespace hrz_proto
{
{% for s in services %}

/*  {{ s.documentation|indent(8) }} */
class I{{ s.full_name|last(".") }}
{
public:
    virtual ~I{{ s.full_name|last(".") }}() = default;

    {% for m in s.methods %}
    /*  {{ m.documentation|indent(12) }} */
    virtual void {{ m.name|snake_case }}(
        const ::{{ m.input|rejoin(".", "::") }}& input,
        ::{{ m.output|rejoin(".", "::") }}& output) = 0;

    {% endfor %}
};

{% endfor %}
} // namespace hrz_proto
