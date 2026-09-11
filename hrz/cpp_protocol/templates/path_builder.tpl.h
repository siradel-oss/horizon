// SPDX-FileCopyrightText: Copyright 2018 Siradel
// SPDX-License-Identifier: MIT

#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <concepts>

#include "hrz/protocol/path_builder_common.h"
#include "hrz/protocol/scene_model/path.pb.h"
{% if filename != "types/wrappers" %}
#include "hrz/protocol/path_builder/types/wrappers.h"
{% endif %}
#include "{{ filename }}.pb.h"

{% for f in dependencies %}
#include "hrz/protocol/path_builder/{{ f[13:] }}.h"
{% endfor %}

namespace hrz_proto
{

{% for type in path_types if type.is_primitive and type.file == filename %}
{% set path_builder_name = type.full_name|to_short_type_name + "PathBuilder" %}
template<std::movable T>
using {{ path_builder_name }} = PrimitivePathBuilder<T, ::hrz_proto::{{ type.full_name|to_wrapper }}, {{ type.full_name|to_cpp_qualified_name }}>;

{% endfor%}

{% for type in path_types if type.is_enum and type.file == filename %}
{% set path_builder_name = type.full_name|to_short_type_name + "PathBuilder" %}
template<std::movable T>
using {{ path_builder_name }} = EnumPathBuilder<T, ::{{ type.full_name|rejoin(".", "::") }}>;

{% endfor%}

{% for type in path_types if not type.is_primitive and not type.is_enum and type.file == filename %}
{% set path_builder_name = type.full_name|to_short_type_name + "PathBuilder" %}
/* {{ type.documentation|indent(4) }} */
template<std::movable T>
class {{ path_builder_name }} : public SceneModelPathBuilder<T, {{ path_builder_name }}<T>>
{
    using Base = SceneModelPathBuilder<T, {{ path_builder_name }}<T>>;

public:
    {{ path_builder_name }}(T&& accessor, hrz_proto::Path&& path) :
        Base{std::move(accessor), std::move(path)}
    {}

    {% if type.is_path_root %}
        {% if type.path_root_type == "HrzProtocol.Void" %}
            template<typename Arg>
            explicit {{ path_builder_name }}(Arg&& accessor)
                requires std::constructible_from<T, Arg&&>
                : Base{std::forward<Arg>(accessor)}
            {
                Base::_path.mutable_root()->mutable_{{ type.path_root }}();
            }
        {% elif type.path_root_type_is_enum %}
            template<typename Arg>
            {{ path_builder_name }}(Arg&& accessor, ::{{ type.path_root_type|rejoin(".", "::") }} {{ type.path_root }})
                requires std::constructible_from<T, Arg&&>
                : Base{std::forward<Arg>(accessor)}
            {
                Base::_path.mutable_root()->set_{{ type.path_root }}({{ type.path_root }});
            }
        {% else %}
            template<typename Arg>
            {{ path_builder_name }}(Arg&& accessor, ::{{ type.path_root_type|rejoin(".", "::") }} {{ type.path_root }})
                requires std::constructible_from<T, Arg&&>
                : Base{std::forward<Arg>(accessor)}
            {
                *(Base::_path.mutable_root()->mutable_{{ type.path_root }}()) = {{ type.path_root }};
            }
        {% endif %}
    {% endif %}

    void set(const {{ type.full_name|to_cpp_qualified_name }}& value) &&
    {
        Base::_accessor.set(std::move(Base::_path), value);
    }

    {{ type.full_name|to_cpp_qualified_name }} get() &&
    {
        return Base::_accessor.template get<{{ type.full_name|to_cpp_qualified_name }}>(std::move(Base::_path));
    }

    {% for u in type.unions %}
    {{ type.full_name|to_cpp_qualified_name }}::{{ u.name|snake_to_pascal }}Case get_{{ u.name }}_case() &&
    {
        Base::_path.add_parts({{ u.fields[0].id }});
        return Base::_accessor.template get_oneof_case<{{ type.full_name|to_cpp_qualified_name }}::{{ u.name|snake_to_pascal }}Case>(std::move(Base::_path));
    }
    {% endfor %}

    {% if not type.is_path_leaf %}
        {% for f in type.fields %}
            {% if not f.repeated %}
                /* {{ f.documentation|indent(4) }} */
                {{ f.type|to_short_type_name }}PathBuilder<T> {{ f.name }}() &&
                {
                    Base::_path.add_parts({{ f.id }});
                    return {{ f.type|to_short_type_name }}PathBuilder<T>(std::move(Base::_accessor), std::move(Base::_path));
                }
            {% else %}
                /* {{ f.documentation|indent(4) }} */
                {{ f.type|to_short_type_name }}PathBuilder<T> {{ f.name }}(uint32_t index) &&
                {
                    Base::_path.add_parts({{ f.id }});
                    Base::_path.add_parts(index);
                    return {{ f.type|to_short_type_name }}PathBuilder<T>(std::move(Base::_accessor), std::move(Base::_path));
                }

                /* Returns the number of {{ f.name }}. */
                uint32_t {{ f.name }}_count() &&
                {
                    Base::_path.add_parts({{ f.id }});
                    return Base::_accessor.count(std::move(Base::_path));
                }

                /* Adds an element to {{ f.name }} and returns the new count. */
                uint32_t add_{{ f.name }}(const {{ f.type|to_cpp_qualified_name }}& value) &&
                {
                    Base::_path.add_parts({{ f.id }});
                    {% if f.is_primitive %}
                        ::hrz_proto::{{ f.type|to_wrapper }} wrapper;
                        wrapper.set_value(value);
                        return Base::_accessor.add_raw(std::move(Base::_path), wrapper.SerializeAsString());
                    {% elif f.is_enum %}
                        ::hrz_proto::UInt32Value wrapper;
                        wrapper.set_value((uint32_t)value);
                        return Base::_accessor.add_raw(std::move(Base::_path), wrapper.SerializeAsString());
                    {% else %}
                        return Base::_accessor.add(std::move(Base::_path), value);
                    {% endif %}
                }

                /* Removes an element from {{ f.name }} and returns the new count. */
                uint32_t remove_{{ f.name }}(uint32_t index) &&
                {
                    Base::_path.add_parts({{ f.id }});
                    Base::_path.add_parts(index);
                    return Base::_accessor.remove(std::move(Base::_path));
                }
            {% endif %}
        {% endfor %}
    {% endif %}
};

{% endfor%}
}
