// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/core.h"
#include "hrz/core/scene_model_accessor.h"

#include "hrz/fnd/log.h"

{% for f in protocol.files %}
#include "{{ f.name }}.pb.h"
{% endfor %}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//        THIS FILE HAS BEEN GENERATED FROM THE SPECS, DO NOT EDIT!!!         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

namespace hrz::scene_model
{

{% for type in path_types %}
{% if not type.is_primitive and not type.is_enum %}

std::string get_message_part_raw(
    const {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path)
{
    if (path.empty())
    {
        return obj.SerializeAsString();
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return get_message_part_raw(obj.{{ f.name|to_cpp_field_name }}(), path);
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated fields need a child index in Get");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size())
                {
                    return get_message_part_raw(obj.{{ f.name|to_cpp_field_name }}(element_index), path.subspan(1));
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return std::string();
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
                return std::string();
            }
        }
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
        {
            ::hrz_proto::{{ f.type|to_wrapper }} wrapper;
            wrapper.set_value(obj.{{ f.name|to_cpp_field_name }}());
            return wrapper.SerializeAsString();
        }
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated fields need a child index in Get");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size())
                {
                    ::hrz_proto::{{ f.type|to_wrapper }} wrapper;
                    wrapper.set_value(obj.{{ f.name|to_cpp_field_name }}(element_index));
                    return wrapper.SerializeAsString();
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return std::string();
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
                return std::string();
            }
        }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            return std::string();
        }
    }
}

void set_message_part_raw(
    {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path,
    std::string_view raw)
{
    if (path.empty())
    {
        (void)obj.ParseFromArray(raw.data(), raw.size());
        return;
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            set_message_part_raw(*obj.mutable_{{ f.name|to_cpp_field_name }}(), path, raw);
            break;
        {% else %}
        case {{ f.id }}:
            assert(!path.empty() && "Repeated fields need a child index in Set");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size())
                {
                    set_message_part_raw(*obj.mutable_{{ f.name|to_cpp_field_name }}(element_index), path.subspan(1), raw);
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
            }
            break;
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
        {
            ::hrz_proto::{{ f.type|to_wrapper }} wrapper;
            (void)wrapper.ParseFromArray(raw.data(), raw.size());
            {% if f.is_enum %}
            obj.set_{{ f.name|to_cpp_field_name }}(({{ f.type|to_cpp_qualified_name }})wrapper.value());
            {% else %}
            obj.set_{{ f.name|to_cpp_field_name }}(wrapper.value());
            {% endif %}
            break;
        }
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated fields need a child index in Set");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size())
                {
                    ::hrz_proto::{{ f.type|to_wrapper }} wrapper;
                    (void)wrapper.ParseFromArray(raw.data(), raw.size());
                    {% if f.is_enum %}
                    obj.set_{{ f.name|to_cpp_field_name }}(element_index, ({{ f.type|to_cpp_qualified_name }})wrapper.value());
                    {% else %}
                    obj.set_{{ f.name|to_cpp_field_name }}(element_index, wrapper.value());
                    {% endif %}
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
            }
            break;
        }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            break;
        }
    }
}

uint32_t count_message_part(
    const {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path)
{
    if (path.empty())
    {
        return 0;
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return count_message_part(obj.{{ f.name|to_cpp_field_name }}(), path);
        {% else %}
        case {{ f.id }}:
            if (path.empty())
            {
                return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
            }
            else
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size())
                {
                    return count_message_part(obj.{{ f.name|to_cpp_field_name }}(element_index), path.subspan(1));
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
                }
            }
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return 0;
        {% else %}
        case {{ f.id }}:
            if (path.empty())
            {
                return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
            }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            return 0;
        }
    }
}

uint32_t add_message_part_raw(
    {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path,
    std::string_view raw)
{
    if (path.empty())
    {
        return 0;
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return add_message_part_raw(*obj.mutable_{{ f.name|to_cpp_field_name }}(), path, raw);
        {% else %}
        case {{ f.id }}:
            if (path.empty())
            {
                auto new_obj = obj.add_{{ f.name|to_cpp_field_name }}();
                (void)new_obj->ParseFromArray(raw.data(), raw.size());
                return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
            }
            else
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size())
                {
                    return add_message_part_raw(*obj.mutable_{{ f.name|to_cpp_field_name }}(element_index), path.subspan(1), raw);
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
                }
            }
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return 0;
        {% else %}
        case {{ f.id }}:
            if (path.empty())
            {
                ::hrz_proto::{{ f.type|to_wrapper }} wrapper;
                (void)wrapper.ParseFromArray(raw.data(), raw.size());
                {% if f.is_enum %}
                obj.add_{{ f.name|to_cpp_field_name }}(({{ f.type|to_cpp_qualified_name }})wrapper.value());
                {% else %}
                obj.add_{{ f.name|to_cpp_field_name }}(wrapper.value());
                {% endif %}
                return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
            }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            return 0;
        }
    }
}

uint32_t remove_message_part(
    {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path)
{
    if (path.empty())
    {
        return 0;
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return remove_message_part(*obj.mutable_{{ f.name|to_cpp_field_name }}(), path);
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated paths need a child index in Remove");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size())
                {
                    if (path.size() == 1)
                    {
                        auto field = obj.mutable_{{ f.name|to_cpp_field_name }}();
                        if (element_index < (uint32_t)field->size())
                        {
                            field->erase(field->begin() + element_index);
                        }

                        return (uint32_t)field->size();
                    }
                    else
                    {
                        uint32_t element_index = path[0];
                        return remove_message_part(*obj.mutable_{{ f.name|to_cpp_field_name }}(element_index), path.subspan(1));
                    }
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
                return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
            }
        }
        {% endif %}
        {% else %}
        {% if not f.repeated %}
        case {{ f.id }}:
            return 0;
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated paths need a child index in Remove");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size())
                {
                    if (path.size() == 1)
                    {
                        auto field = obj.mutable_{{ f.name|to_cpp_field_name }}();
                        if (element_index < (uint32_t)field->size())
                        {
                            field->erase(field->begin() + element_index);
                        }

                        return (uint32_t)field->size();
                    }
                    else
                    {
                        HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
                        return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
                    }
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
                return (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size();
            }
        }
        {% endif %}
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            HRZ_LOG_ERROR("Invalid path in \"{{ type.full_name }}\": {}", this_root);
            assert(0 && "Wrong path!");
            return 0;
        }
    }
}


uint32_t get_oneof_case_part_raw(
    const {{ type.full_name|to_cpp_qualified_name }}& obj,
    std::span<const uint32_t> path)
{
    if (path.empty())
    {
        return 0;
    }

    uint32_t this_root = path[0];
    path = path.subspan(1);

    switch (this_root)
    {
        {% if not type.is_path_leaf %}
        {% for f in type.fields %}
        {% if not f.is_primitive and not f.is_enum %}
        {% if not f.repeated %}
        case {{ f.id }}:
        {% if f.union %}
        {
            if (path.empty())
            {
                return obj.{{ f.union }}_case();
            }
            else
            {
                return get_oneof_case_part_raw(obj.{{ f.name|to_cpp_field_name }}(), path);
            }
        }
        {% else %}
            return get_oneof_case_part_raw(obj.{{ f.name|to_cpp_field_name }}(), path);
        {% endif %}
        {% else %}
        case {{ f.id }}:
        {
            assert(!path.empty() && "Repeated fields need a child index in Get");
            if (!path.empty())
            {
                uint32_t element_index = path[0];
                if (element_index < (uint32_t)obj.{{ f.name|to_cpp_field_name }}_size())
                {
                    return get_oneof_case_part_raw(obj.{{ f.name|to_cpp_field_name }}(element_index), path.subspan(1));
                }
                else
                {
                    HRZ_LOG_ERROR("Invalid \"{{ f.name }}\" index in \"{{ type.full_name }}\": {}", element_index);
                    return 0;
                }
            }
            else
            {
                HRZ_LOG_ERROR("Missing \"{{ f.name }}\" index in \"{{ type.full_name }}\"");
                return 0;
            }
        }
        {% endif %}
        {% else %}
        {% if not f.repeated and f.union %}
        case {{ f.id }}:
            return path.empty() ? obj.{{ f.union }}_case() : 0;
        {% endif %}
        {% endif %}
        {% endfor %}
        {% else %}
        {% for f in type.fields %}
        {% if f.union %}
        case {{ f.id }}:
            return path.empty() ? obj.{{ f.union }}_case() : 0;
        {% endif %}
        {% endfor %}
        {% endif %}
        default:
        {
            return 0;
        }
    }
}


{% endif %}
{% endfor %}

}
