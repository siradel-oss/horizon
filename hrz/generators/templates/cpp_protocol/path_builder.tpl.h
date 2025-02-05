#pragma once

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

{% for f in files %}
#include "{{ f }}.pb.h"
{% endfor %}

namespace hrz_proto
{

template<typename T>
struct SceneModelPathBuilder
{
    T                   _accessor;
    HrzProtocol::Path   _path;
};

{% for type in path_types %}
template<typename T>
class {{ type.full_name|to_short_type_name }}PathBuilder;
{% endfor %}

{% for type in path_types %}
template<typename T>
class {{ type.full_name|to_short_type_name }}PathBuilder : public SceneModelPathBuilder<T>
{
public:
    {{ type.full_name|to_short_type_name }}PathBuilder() = default;

    {% if type.is_path_root %}
    {% if type.path_root_type == "HrzProtocol.Void" %}
    {{ type.full_name|to_short_type_name }}PathBuilder(const T& accessor)
    {
        this->_accessor = accessor;
        *(this->_path.mutable_root()->mutable_{{ type.path_root }}()) = ::HrzProtocol::Void();
    }
    {% elif type.path_root_type_is_enum %}
    {{ type.full_name|to_short_type_name }}PathBuilder(const T& accessor, const ::{{ type.path_root_type|rejoin(".", "::") }}& {{ type.path_root }})
    {
        this->_accessor = accessor;
        this->_path.mutable_root()->set_{{ type.path_root }}({{ type.path_root }});
    }
    {% else %}
    {{ type.full_name|to_short_type_name }}PathBuilder(const T& accessor, const ::{{ type.path_root_type|rejoin(".", "::") }}& {{ type.path_root }})
    {
        this->_accessor = accessor;
        *(this->_path.mutable_root()->mutable_{{ type.path_root }}()) = {{ type.path_root }};
    }
    {% endif %}
    {% endif %}

    {{ type.full_name|to_short_type_name }}PathBuilder<T> clone() const
    {
        {{ type.full_name|to_short_type_name }}PathBuilder<T> p;
        p._accessor = this->_accessor;
        p._path = this->_path;
        return p;
    }

    void set(const {{ type.full_name|to_cpp_qualified_name }}& value)
    {
        {% if type.is_primitive %}
        ::HrzProtocol::{{ type.full_name|to_wrapper }} wrapper;
        wrapper.set_value(value);
        this->_accessor.set_raw(std::move(this->_path), wrapper.SerializeAsString());
        {% elif type.is_enum %}
        ::HrzProtocol::UInt32Value wrapper;
        wrapper.set_value((uint32_t)value);
        this->_accessor.set_raw(std::move(this->_path), wrapper.SerializeAsString());
        {% else %}
        this->_accessor.set(std::move(this->_path), value);
        {% endif %}
    }

    {{ type.full_name|to_cpp_qualified_name }} get()
    {
        {% if type.is_primitive %}
        auto raw = this->_accessor.get_raw(std::move(this->_path));
        ::HrzProtocol::{{ type.full_name|to_wrapper }} wrapper;
        wrapper.ParseFromString(raw);
        return wrapper.value();
        {% elif type.is_enum %}
        auto raw = this->_accessor.get_raw(std::move(this->_path));
        ::HrzProtocol::UInt32Value wrapper;
        wrapper.ParseFromString(raw);
        return ({{ type.full_name|to_cpp_qualified_name }})wrapper.value();
        {% else %}
        return this->_accessor.template get<{{ type.full_name|to_cpp_qualified_name }}>(std::move(this->_path));
        {% endif %}
    }

    {% if not type.is_path_leaf %}
    {% for f in type.fields %}
    {% if not f.repeated %}
    /* {{ f.documentation|indent(4) }} */
    {{ f.type|to_short_type_name }}PathBuilder<T> {{ f.name }}()
    {
        {{ f.type|to_short_type_name }}PathBuilder<T> p;
        p._accessor = this->_accessor;
        p._path = std::move(this->_path);
        p._path.add_parts({{ f.id }});
        return p;
    }
    {% else %}

    /* {{ f.documentation|indent(4) }} */
    {{ f.type|to_short_type_name }}PathBuilder<T> {{ f.name }}(uint32_t index)
    {
        {{ f.type|to_short_type_name }}PathBuilder<T> p;
        p._accessor = this->_accessor;
        p._path = std::move(this->_path);
        p._path.add_parts({{ f.id }});
        p._path.add_parts(index);
        return p;
    }

    /* Returns the number of {{ f.name }}. */
    uint32_t {{ f.name }}_count()
    {
        this->_path.add_parts({{ f.id }});
        return this->_accessor.count(std::move(this->_path));
    }

    /* Adds an element to {{ f.name }} and returns the new count. */
    uint32_t add_{{ f.name }}(const {{ f.type|to_cpp_qualified_name }}& value)
    {
        this->_path.add_parts({{ f.id }});
        {% if f.is_primitive %}
        ::HrzProtocol::{{ f.type|to_wrapper }} wrapper;
        wrapper.set_value(value);
        return this->_accessor.add_raw(std::move(this->_path), wrapper.SerializeAsString());
        {% elif f.is_enum %}
        ::HrzProtocol::UInt32Value wrapper;
        wrapper.set_value((uint32_t)value);
        return this->_accessor.add_raw(std::move(this->_path), wrapper.SerializeAsString());
        {% else %}
        return this->_accessor.add(std::move(this->_path), value);
        {% endif %}
    }

    /* Removes an element from {{ f.name }} and returns the new count. */
    uint32_t remove_{{ f.name }}(uint32_t index)
    {
        this->_path.add_parts({{ f.id }});
        this->_path.add_parts(index);
        return this->_accessor.remove(std::move(this->_path));
    }

    {% endif %}

    {% endfor %}
    {% endif %}
};

{% endfor%}
}
